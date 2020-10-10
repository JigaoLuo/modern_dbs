#include "slotted_page/slotted_page.h"
#include "slotted_page/hex_dump.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <vector>

using moderndbs::SlottedPage;

uint32_t SlottedPage::get_continuous_free_space() {
    /// Detail referring to Slotted page format in slotted_page.h.
    const auto last_slot_end = (header.slot_count * sizeof(Slot)) + sizeof(Header);
    const auto last_data_start = header.data_start;
    assert(last_data_start >= last_slot_end && "should never overlap!");
    return last_data_start - last_slot_end;
}

uint16_t SlottedPage::allocate(uint32_t data_size, uint32_t page_size) {
    /// 1. Check if we can reuse a slot or to new a slot.
    const uint16_t slot_id = [&] {
        if (header.first_free_slot == header.slot_count) {
            /// 1.1. Case := New a slot.
            header.first_free_slot++;
            assert(header.free_space >= data_size + sizeof(Slot));
            if (get_continuous_free_space() < data_size + sizeof(Slot)) {
                /// Needs compatification.
                compactify(page_size);
            }
            header.free_space -= (data_size + sizeof(Slot));
            return header.slot_count++;
        } else {
            /// 1.2. Case := Reuse a empty slot.
            assert(header.first_free_slot < header.slot_count);
            const uint16_t slot_id = header.first_free_slot;
            if (get_continuous_free_space() < data_size) {
                /// Needs compatification.
                compactify(page_size);
            }
            /// Find the next `first_free_slot` after this found slot.
            for (uint16_t i = header.first_free_slot + 1; i < header.slot_count; i++) {
                if (is_empty_slot(i)) {
                    header.first_free_slot = i;
                }
            }
            /// If nothing found in the last loop, then necessary to update first_free_slot.
            if (slot_id == header.first_free_slot) {
                header.first_free_slot = header.slot_count;
            }
            assert(header.free_space >= data_size);
            header.free_space -= data_size;
            return slot_id;
        }
    } ();

    /// 2. Update data start.
    assert(header.data_start >= data_size);
    header.data_start -= data_size;
    /// 3. Set slot.
    set_slot(slot_id, header.data_start, data_size);
    std::memset(get_data() + header.data_start, 0x00, data_size);
    return slot_id;
}

void SlottedPage::relocate(uint16_t slot_id, uint32_t data_size, uint32_t page_size) {
    assert(slot_id < header.slot_count);
    /// 1. Get free space and update.
    Slot *slot = get_slot_ptr(slot_id);
    assert(!slot->is_empty());
    const uint32_t slot_size = slot->get_size();
    const uint32_t slot_offset = slot->get_offset();
    /// If relocate a redirecting slotted page, then must be an empty slot := slot->is_redirect() implies slot_size == 0.
    assert(!slot->is_redirect() || slot_size == 0);

    /// 2. Easy Cases
    if (slot_size == 0 && slot_offset == 1) {
        /// Special case: Redirecting page to non-redirecting page.
        assert(header.free_space >= data_size);
        if (slot_offset == header.data_start) {
          header.data_start += slot_size;
        }
        /// Clear this slot only temporarily, re-set it inplace at end of this function.
        slot->clear();
        /// Check free space, compactify if necessary.
        if (get_continuous_free_space() < data_size) {
          compactify(page_size);
        }
        /// Update free space and data start.
        header.free_space -= data_size;
        header.data_start -= data_size;
        /// Set slot and restore data.
        slot->set_slot(header.data_start, data_size);
        std::memset(get_data() + header.data_start + slot_size, 0x00, data_size);
        /// No data copying in this function.
        /// Data copy is done by caller function -- sp_segment's resize function.
        return;
    } else if (slot_size == data_size) {
        /// Easy Case: relocate to same size.
        return;
    } else if (slot_size > data_size) {
        /// Easy Case: relocate to less size, losing data at end.
        /// If is redirect target, then must have larger than TID size.
        assert(!slot->is_redirect_target() || data_size > sizeof(TID));
        header.free_space += (slot_size - data_size);
        slot->set_size(data_size);
        return;
    }

    /// 3. Hard Case: relocate to larger size := slot_size < data_size.
    assert(slot_size < data_size);
    /// 3.1. Copy data out in a temp buffer, since the compactify function cleans out data area of empty slots.
    std::vector<std::byte> buffer;
    buffer.resize(slot_size);
    std::memcpy(buffer.data(), get_data() + slot_offset, slot_size);
    /// 3.2. Free the data, since data is cached in buffer.
    header.free_space += slot_size;
    assert(header.free_space >= data_size);
    if (slot_offset == header.data_start) {
        header.data_start += slot_size;
    }
    /// 3.3. Clear this slot only temporarily, re-set it inplace at end of this function.
    slot->clear();
    /// 3.4. Check free space, compactify if necessary.
    if (get_continuous_free_space() < data_size) {
        compactify(page_size);
    }
    /// 3.5. Update free space and data start.
    header.free_space -= data_size;
    header.data_start -= data_size;
    /// 3.6. Set slot and restore data.
    slot->set_slot(header.data_start, data_size);
    std::memcpy(get_data() + header.data_start, buffer.data(), slot_size);
    std::memset(get_data() + header.data_start + slot_size, 0, data_size - slot_size);
    return;
}

void SlottedPage::erase(uint16_t slot_id) {
    assert(slot_id < header.slot_count);
    /// 0. Get slot.
    Slot *slot = get_slot_ptr(slot_id);
    assert(!slot->is_redirect());
    assert(!slot->is_empty());
    /// 1. Update free space.
    header.free_space += slot->get_size();
    /// 2. Check if a new first_free_slot is generated.
    if (slot_id < header.first_free_slot) {
        header.first_free_slot = slot_id;
    }
    /// 3. Check if data_start is changed.
    if (slot->get_offset() == header.data_start) {
        header.data_start += slot->get_size();
    }
    clear_slot(slot_id);
    assert(is_empty_slot(slot_id));
    /// 4. If this slot is the last slot, free space and slot if necessary.
    if (slot_id == header.slot_count - 1) {
        while (header.slot_count >= 1 && is_empty_slot(header.slot_count - 1)) {
            header.slot_count--;
            header.free_space += sizeof(Slot);
        }
    }
}

void SlottedPage::compactify(uint32_t page_size) {
    /// Not to invalid slot id.
    /// so free_space can't be used all, if some slot are empty in the middle ==> slots.size < slot_counter.
    /// Thus we only compactify the data at the end of page.

    /// 0. Bookkeeping.
    uint32_t last_data_offset = 0;
    uint32_t offset_shift = 0;  // a.k.a saved, re-used bytes by compactify

    /// 1. Cache all slots and sort by descending offset.
    ///    To guarantee compactify starts with the data area with largest offset.
    std::vector<Slot*> slots;
    ///    maximal header.slot_count slots. If some are empty slots, then less.
    slots.reserve(header.slot_count);
    for (uint16_t i = 0; i < header.slot_count; i++) {
        Slot *slot = get_slot_ptr(i);
        if (!slot->is_redirect() && !slot->is_empty()) {
            slots.emplace_back(slot);
        }
    }
    assert(slots.size() <= header.slot_count);

    /// If all slots are empty, then no data area in this page.
    if (slots.size() == 0) {
        header.data_start = page_size;
        return;
    }

    std::sort(slots.begin(), slots.end(), [](const Slot* lhs, const Slot* rhs) { return lhs->get_offset() > rhs->get_offset(); });

    /// 2. Move the last data area to the end of page, if necessary.
    last_data_offset = slots[0]->get_offset();
    if (last_data_offset + slots[0]->get_size() < page_size) {
        offset_shift += (page_size - (last_data_offset + slots[0]->get_size()));
        std::memmove(reinterpret_cast<std::byte*>(this) + last_data_offset + offset_shift,
                     reinterpret_cast<std::byte*>(this) + last_data_offset,
                     slots[0]->get_size());
        slots[0]->set_offset(last_data_offset + offset_shift);
        last_data_offset += offset_shift;
    }

    /// 3. Iterate all slots, checking if they have continuous data area. Compactify, if any gap exists.
    for (size_t i = 1; i < slots.size(); i++) {
        auto slot = slots[i];
        assert(!slot->is_redirect() && !slot->is_empty());
        {
            const uint32_t data_size = slot->get_size();
            const uint32_t data_offset = slot->get_offset();
            assert(data_offset + data_size + offset_shift <= last_data_offset);
            /// Empty between two data element, non-continuous data area.
            /// if last_data_offset - (data_size + data_offset + offset_shift) != 0.
            offset_shift += (last_data_offset - (data_size + data_offset + offset_shift));
            std::memmove(reinterpret_cast<std::byte*>(this) + data_offset + offset_shift,
                         reinterpret_cast<std::byte*>(this) + data_offset,
                         data_size);
            slot->set_offset(data_offset + offset_shift);
        }
        last_data_offset = slot->get_offset();
    }
    header.data_start = last_data_offset;
}
