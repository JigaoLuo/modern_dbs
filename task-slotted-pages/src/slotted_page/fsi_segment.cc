#include "slotted_page/defer.h"
#include "slotted_page/segment.h"
#include <functional>
#include <limits>

using Defer = moderndbs::Defer;
using FSISegment = moderndbs::FSISegment;
using Segment = moderndbs::Segment;

FSISegment::FSISegment(segment_id_t segment_id, BufferManager& buffer_manager, schema::Table& table) : Segment(segment_id, buffer_manager), table(table) {
    /// 1. Init the look up table using free size on a page.
    const auto free_size = buffer_manager.get_page_size() - sizeof(SlottedPage::Header);
    /// Encoding upper half in linear scale.
    const auto half_page_size = free_size >> 1;
    const auto linear_level = half_page_size >> 3;
    look_up_table[15] = free_size;
    look_up_table[14] = free_size - 1 * linear_level;
    look_up_table[13] = free_size - 2 * linear_level;
    look_up_table[12] = free_size - 3 * linear_level;
    look_up_table[11] = free_size - 4 * linear_level;
    look_up_table[10] = free_size - 5 * linear_level;
    look_up_table[9]  = free_size - 6 * linear_level;
    look_up_table[8]  = free_size - 7 * linear_level;

    ///  Encoding lower half in logarithmic scale.
    look_up_table[7] = half_page_size;
    look_up_table[6] = half_page_size >> 1;
    look_up_table[5] = half_page_size >> 2;
    look_up_table[4] = half_page_size >> 3;
    look_up_table[3] = half_page_size >> 4;
    look_up_table[2] = half_page_size >> 5;
    look_up_table[1] = half_page_size >> 6;
    look_up_table[0] = 0;

    /// 2. Init a FSI page, which can cover 2048 slotted pages.
    /// With a page_size of 1024 KiB - a entry for 4bit => each FSI page contain 2048 Entries mapping 2048 slotted pages => [0, 2047].
    table.allocated_fsi_pages = 1;
    /// 2.1. The first page of fsi segment.
    const page_id_t fsi_page_id = static_cast<uint64_t>(table.fsi_segment) << 48;
    BufferFrame& buffer_frame = buffer_manager.fix_page(fsi_page_id, true);
    /// 2.2. Init all bytes with 0xFF, for 2048 slotted pages in advance.
    std::memset(buffer_frame.get_page_raw_data(), 0xFF, buffer_manager.get_page_size());
    buffer_manager.unfix_page(buffer_frame, true);
}

std::tuple<moderndbs::page_id_t, uint32_t, bool> FSISegment::get_fsi_page(page_id_t target_page) {
    /// With a page_size of 1024 KiB - a entry for 4bit => each FSI page contain 2048 Entries mapping 2048 slotted pages => [0, 2047].
    /// 1. Get slotted page's page id without segment id.
    const uint64_t segment_page_id = get_segment_page_id(target_page);
    /// page id without segment id is out of bounds => no matching FSI and return false.
    if (segment_page_id >= table.allocated_slotted_pages) {
        return std::make_tuple(0, 0, false);
    }
    const auto page_size = buffer_manager.get_page_size();
    /// 1. Get FSI page's page id without segment id.
    ///    Each FSI Page carry (page_size * 2) FSI entries, since each Byte carries two FSI entries.
    ///    page_size << 1 == page_size * 2.
    const auto fsi_segment_page_id = segment_page_id / (page_size << 1);
    const page_id_t fsi_page_id = (static_cast<page_id_t>(this->segment_id) << 48) ^ fsi_segment_page_id;
    /// 3. Get FSI entry offset of the FSI page.
    ///    page_size << 1 == page_size * 2.
    const uint32_t page_offset = segment_page_id % (page_size << 1);
    return std::make_tuple(fsi_page_id, page_offset, true);
}

moderndbs::page_id_t FSISegment::get_target_page(page_id_t fsi_page, uint32_t entry) {
    /// With a page_size of 1024 KiB - a entry for 4bit => each FSI page contain 2048 Entries mapping 2048 slotted pages => [0, 2047].
    /// 1. Get FSI page's page id without segment id.
    const file_offset segment_page_id = get_segment_page_id(fsi_page);
    const auto page_size = buffer_manager.get_page_size();
    /// 2. Get absolute offset of the target slotted page id.
    ///    Each FSI Page carry (page_size * 2) FSI entries, since each Byte carries two FSI entries.
    ///    page_size << 1 == page_size * 2.
    const file_offset target_segment_page_id = segment_page_id * (page_size << 1) + entry;
    /// 3. Build the page id with segment id.
    return (static_cast<page_id_t>(table.sp_segment) << 48) ^ target_segment_page_id;
}

uint8_t FSISegment::encode_free_space(uint32_t free_space) {
    /// Find the lower_bound, it always returns.
    for (uint8_t i = 15; ; i--) {
        if (free_space >= look_up_table[i]) { return i; }
    }
}

uint32_t FSISegment::decode_free_space(uint8_t free_space) { return look_up_table[free_space]; }

void FSISegment::update(uint64_t target_page, uint32_t free_space) {
    /// 1. Get the page_id and the offset on FSI page.
    const auto result_tuple = get_fsi_page(target_page);
    assert(std::get<2>(result_tuple));
    const page_id_t fsi_page = std::get<0>(result_tuple);
    const uint32_t fsi_page_offset = std::get<1>(result_tuple);
    /// 2. Fix this FSI page.
    BufferFrame& bf = buffer_manager.fix_page(fsi_page, true);
    char* data = bf.get_page_raw_data();
    /// 3. Get the Entry as a byte => actually two entries.
    /// fsi_page_offset / 2 == fsi_page_offset >> 1.
    auto *entry = reinterpret_cast<uint8_t*>(data + (fsi_page_offset >> 1));
    /// 4. Update the entry inplace.
    /// Pay attention to, if the offset is a ODD/EVEN number.
    if ((fsi_page_offset & 1) == 0) {
        /// Case: offset is even => entry is the first 4 bits.
        *entry = (encode_free_space(free_space) << 4) + (*entry & 0x0f);
    } else {
        /// Case: offset is odd => entry is the last 4 bits.
        *entry = (*entry & 0xf0) + encode_free_space(free_space);
    }
    buffer_manager.unfix_page(bf, true);
}

std::pair<bool, moderndbs::page_id_t> FSISegment::find(uint32_t required_space) {
    /// 0. Preparation.
    const page_id_t fsi_segment_shifted = static_cast<page_id_t>(this->segment_id) << 48;
    /// Number of allocated slotted pages.
    const uint64_t num_sp = table.allocated_slotted_pages;
    assert(num_sp > 0);
    const uint64_t num_fsi_page = table.allocated_fsi_pages;
    assert(num_fsi_page > 0);
    const auto page_size = buffer_manager.get_page_size();
    /// Each slotted page must be mapped to a FSI on a FSI page.
    /// With a page_size of 1024 KiB - a entry for 4bit => each FSI page contain 2048 Entries mapping 2048 slotted pages => [0, 2047].
    assert(table.allocated_slotted_pages * (page_size << 1) >= num_sp);

    /// 1. Iterate all slotted pages' FSI entry to find required_space.
    for (uint64_t fsi_page = 0, sp = 0; fsi_page < num_fsi_page; fsi_page++) {
        /// 1.1. Get the FSI page_id.
        page_id_t fsi_page_id = fsi_segment_shifted | fsi_page;
        /// 1.2. Fix this FSI page covering 2048 slotted pages.
        BufferFrame& bf = buffer_manager.fix_page(fsi_page_id, true);
        /// 1.3. Get the data a.k.a FSI entries.
        char* data = bf.get_page_raw_data();
        /// 1.4. Read the entry, each byte is two entries.
        /// Pay attention to, if the offset is a ODD/EVEN number.
        for (size_t byte = 0; byte < page_size; byte++) {
            auto *entry = reinterpret_cast<uint8_t*>(data + byte);

            /// first 4 bits := first entry.
            const uint8_t first = *entry >> 4;
            uint32_t free_space = decode_free_space(first);
            if (free_space >= required_space) {
                *entry = ((encode_free_space(free_space - required_space)) << 4) | (*entry & 0x0f);
                buffer_manager.unfix_page(bf, true);
                /// byte << 1 == byte * 2.
                return {true, get_target_page(fsi_page_id, byte << 1)};
            }
            /// If all slotted pages iterated, nothing fit found, then return false.
            sp++;
            if (sp == num_sp) {
                buffer_manager.unfix_page(bf, false);
                return {false, ((static_cast<uint64_t>(this->table.sp_segment) << 48) ^ this->table.allocated_slotted_pages)};
            }

            /// second 4 bits := second entry.
            const uint8_t second = *entry & 0x0f;
            free_space = decode_free_space(second);
            if (free_space >= required_space) {
                *entry = (*entry & 0xf0) | encode_free_space(free_space - required_space);
                buffer_manager.unfix_page(bf, true);
                // byte << 1 == byte * 2
                return {true, get_target_page(fsi_page_id, (byte << 1) + 1)};
            }
            /// If all slotted pages iterated, nothing fit found, then return false.
            sp++;
            if (sp == num_sp) {
                buffer_manager.unfix_page(bf, false);
                return {false, ((static_cast<uint64_t>(this->table.sp_segment) << 48) ^ this->table.allocated_slotted_pages)};
            }
        }
        /// Unfix current page.
        buffer_manager.unfix_page(bf, false);
    }
    /// Should not reach here, return should be done in the for-loop.
    __builtin_unreachable();  /// Oh! This function works with GCC and Clang. xD.
}
