#include "slotted_page/defer.h"
#include "slotted_page/hex_dump.h"
#include "slotted_page/segment.h"
#include "slotted_page/slotted_page.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <functional>

using moderndbs::SPSegment;
using moderndbs::Segment;
using moderndbs::TID;

SPSegment::SPSegment(segment_id_t segment_id, BufferManager& buffer_manager, SchemaSegment &schema, FSISegment &fsi, schema::Table& table) : Segment(segment_id, buffer_manager), schema(schema), fsi(fsi), table(table) {
    /// 1. Init the first slotted page.
    table.allocated_slotted_pages = 1;
    const page_id_t sp_page_id = static_cast<page_id_t>(table.sp_segment) << 48;
    BufferFrame& buffer_frame = buffer_manager.fix_page(sp_page_id, true);
    [[maybe_unused]] const auto slotted_page = new (buffer_frame.get_page_raw_data()) SlottedPage(buffer_manager.get_page_size());
    assert(slotted_page->header.slot_count == 0);
    assert(slotted_page->header.first_free_slot == 0);
    assert(slotted_page->header.data_start == buffer_manager.get_page_size());
    assert(slotted_page->header.free_space ==  buffer_manager.get_page_size() - sizeof(SlottedPage::Header));
    buffer_manager.unfix_page(buffer_frame, true);
}

TID SPSegment::allocate(uint32_t required_space) {
    /// 0. Check the pre-conditions to allocate.
    assert(table.allocated_slotted_pages > 0);
    assert(table.allocated_fsi_pages > 0);
    assert(required_space <= buffer_manager.get_page_size() - sizeof(SlottedPage::Header) - sizeof(SlottedPage::Slot));
    /// 1. Try to find space in FSI.
    ///    The slotted page page_id that we found.
    ///       fsi_found == true, target_page_id is the found slotted page.
    ///       fsi_found == false, target_page_id is the to be allocated slotted page.
    const auto [fsi_found, target_page_id] = fsi.find(required_space + sizeof(SlottedPage::Slot));
    const auto segment_page_id = get_segment_page_id(target_page_id);

    /// 2. Check if a target slotted page is found.
    if (!fsi_found && table.allocated_slotted_pages++ == segment_page_id) {
        /// 3. Case Hard: No target slotted page found, then need to allocate a new slotted page.
        /// 3.1 Allocate new slotted page.
        BufferFrame& buffer_frame = buffer_manager.fix_page(target_page_id, true);
        const auto slotted_page = new (buffer_frame.get_page_raw_data()) SlottedPage(buffer_manager.get_page_size());
        assert(slotted_page->header.slot_count == 0);
        assert(slotted_page->header.first_free_slot == 0);
        assert(slotted_page->header.data_start == buffer_manager.get_page_size());
        assert(slotted_page->header.free_space ==  buffer_manager.get_page_size() - sizeof(SlottedPage::Header));

        /// 3.2. If necessary, allocate new FSI page covering the new 2048 slotted pages in the future.
        ///      buffer_manager.get_page_size() << 1 == buffer_manager.get_page_size() * 2.
        if (table.allocated_slotted_pages % (buffer_manager.get_page_size() << 1) == 0) {
            /// With a page_size of 1024 KiB - a entry for 4bit => each FSI page contain 2048 Entries mapping 2048 slotted pages => [0, 2047].
            /// 1. The first page of fsi segment.
            const page_id_t fsi_page_id = static_cast<page_id_t>(table.fsi_segment) << 48 | (table.allocated_fsi_pages++);
            BufferFrame& buffer_frame = buffer_manager.fix_page(fsi_page_id, true);
            /// 2. Init all bytes with 0xFF, for 2048 slotted pages in advance.
            std::memset(buffer_frame.get_page_raw_data(), 0xFF, buffer_manager.get_page_size());
            buffer_manager.unfix_page(buffer_frame, true);
        }

        /// 3.3. Do the allocation.
        const uint16_t slot_id = slotted_page->allocate(required_space, buffer_manager.get_page_size());
        /// 3.4. Unfix page.
        const uint32_t free_space = slotted_page->header.free_space;
        buffer_manager.unfix_page(buffer_frame, true);
        /// 3.5. On new page, update the FSI entry for this new allocated slotted page.
        fsi.update(target_page_id, free_space);
        return TID(target_page_id, slot_id);
    } else {
        /// 4. Case Simple: Allocate tuple in the found slotted page.
        /// 4.1. Fix page.
        auto& bufferframe = buffer_manager.fix_page(target_page_id, true);
        /// 4.2. reinterpret_cast to SlottedPage*.
        auto slotted_page = reinterpret_cast<SlottedPage*>(bufferframe.get_page_raw_data());
        /// 4.3. allocate this size in this page => return the slot, where allocated.
        const uint16_t slot_id = slotted_page->allocate(required_space, buffer_manager.get_page_size());
        /// 4.4. Unfix page.
        buffer_manager.unfix_page(bufferframe, true);
        /// 4.5. On old page, FSI is already updated by calling fsi.find function.
        return TID(target_page_id, slot_id);
    }
}

uint32_t SPSegment::read(TID tid, std::byte *record, uint32_t capacity) const {
    /// This function can be done recursively for the case of Indirection. I decided not to do so, in order to check my assertions.

    /// 1. Get page id.
    const page_id_t page_id = tid.get_page_id(segment_id);
    /// 2. Get slot id.
    const uint16_t slot_id = tid.get_slot();
    /// 3. Fix page.
    BufferFrame& buffer_frame = buffer_manager.fix_page(page_id, false);
    auto const slotted_page = reinterpret_cast<SlottedPage*>(buffer_frame.get_page_raw_data());
    /// 4. Get slot.
    SlottedPage::Slot *slot_to_read = slotted_page->get_slot_ptr(slot_id);
    const auto slot_size = slot_to_read->get_size();
    const auto slot_offset = slot_to_read->get_offset();

    /// Cases to read data.
    if (!slot_to_read->is_redirect()) {
        /// Case Easy: directly read.
        assert(slot_size >= capacity);  /// can read less than the size.
        /// 5. Read.
        std::memcpy(record, slotted_page->get_data() + slot_offset, capacity);
        /// 6. Unfix page.
        buffer_manager.unfix_page(buffer_frame, false);
        return capacity;
    } else {
        /// Case Hard: read redirection.
        /// 5. Read redirect target TID.
        TID redirect_target_tid{slot_to_read->as_redirect_tid()};
        /// 6. Unfix redirecting page.
        buffer_manager.unfix_page(buffer_frame, false);
        /// 7. Get redirect target page id.
        const page_id_t redirect_target_page_id = redirect_target_tid.get_page_id(segment_id);
        /// 8. Get redirect target slot id.
        const uint16_t redirect_target_slot_id = redirect_target_tid.get_slot();
        /// 9. Fix page.
        BufferFrame& redirect_target_buffer_frame = buffer_manager.fix_page(redirect_target_page_id, false);
        auto const redirect_target_slotted_page = reinterpret_cast<SlottedPage*>(redirect_target_buffer_frame.get_page_raw_data());
        /// 10. Get slot.
        slot_to_read = redirect_target_slotted_page->get_slot_ptr(redirect_target_slot_id);
        const auto redirect_target_slot_size = slot_to_read->get_size();
        const auto redirect_target_slot_offset = slot_to_read->get_offset();
        assert(slot_to_read->is_redirect_target());
        assert(redirect_target_slot_size > sizeof(TID));
        assert(redirect_target_slot_size >= capacity);
        /// 11. Read original TID and check.
        TID original_tid{0};
        std::memcpy(&original_tid, redirect_target_slotted_page->get_data() + redirect_target_slot_offset, sizeof(TID));
        assert(original_tid.get_value() == tid.get_value());
        /// 12. Read data after original TID.
        std::memcpy(record, redirect_target_slotted_page->get_data() + redirect_target_slot_offset + sizeof(TID), capacity);
        /// 13. Unfix page.
        buffer_manager.unfix_page(redirect_target_buffer_frame, false);
        return capacity;
    }
}

uint32_t SPSegment::write(TID tid, std::byte *record, uint32_t record_size) {
    /// This function can be done recursively for the case of Indirection. I decided not to do so, in order to check my assertions.
    /// 1. Get page id.
    const page_id_t page_id = tid.get_page_id(this->segment_id);
    /// 2. Get slot id.
    const uint16_t slot_id = tid.get_slot();
    /// 3. Fix page.
    BufferFrame& buffer_frame = buffer_manager.fix_page(page_id, true);
    auto const slotted_page = reinterpret_cast<SlottedPage*>(buffer_frame.get_page_raw_data());
    /// 4. Get slot.
    SlottedPage::Slot *slot_to_write = slotted_page->get_slot_ptr(slot_id);
    const auto slot_size = slot_to_write->get_size();
    const auto slot_offset = slot_to_write->get_offset();

    /// Cases to write data
    if (!slot_to_write->is_redirect()) {
        /// Case Easy: directly write.
        assert(slot_size >= record_size);  /// can read write than the size.
        /// 5. Write.
        std::memcpy(slotted_page->get_data() + slot_offset, record, record_size);
        /// 6. Unfix page.
        buffer_manager.unfix_page(buffer_frame, true);
        return record_size;
    } else {
        /// Case Hard: write redirection.
        /// 5. Write redirect target TID.
        TID redirect_target_tid{slot_to_write->as_redirect_tid()};
        std::memcpy(&redirect_target_tid, slotted_page->get_data() + slot_offset, sizeof(TID));
        /// 6. Unfix redirecting page -- no change on that.
        buffer_manager.unfix_page(buffer_frame, false);
        /// 7. Get redirect target page id.
        const page_id_t redirect_target_page_id = redirect_target_tid.get_page_id(segment_id);
        /// 8. Get redirect target slot id.
        const uint16_t redirect_target_slot_id = redirect_target_tid.get_slot();
        /// 9. Fix page.
        BufferFrame& redirect_target_buffer_frame = buffer_manager.fix_page(redirect_target_page_id, true);
        auto const redirect_target_slotted_page = reinterpret_cast<SlottedPage*>(redirect_target_buffer_frame.get_page_raw_data());
        /// 10. Get slot.
        slot_to_write = redirect_target_slotted_page->get_slot_ptr(redirect_target_slot_id);
        const auto redirect_target_slot_size = slot_to_write->get_size();
        const auto redirect_target_slot_offset = slot_to_write->get_offset();
        assert(slot_to_write->is_redirect_target());
        assert(redirect_target_slot_size > sizeof(TID));
        assert(redirect_target_slot_size >= record_size);
        /// 11. Read original TID and check.
        TID original_tid{0};
        std::memcpy(&original_tid, redirect_target_slotted_page->get_data() + redirect_target_slot_offset, sizeof(TID));
        assert(original_tid.get_value() == tid.get_value());
        /// 12. Write data after original TID.
        std::memcpy(redirect_target_slotted_page->get_data() + redirect_target_slot_offset + sizeof(TID), record, record_size);
        /// 13. Unfix page.
        buffer_manager.unfix_page(redirect_target_buffer_frame, true);
        return record_size;
    }
}

void SPSegment::resize(TID tid, uint32_t new_size) {
    /// 1. Get page id.
    const page_id_t page_id = tid.get_page_id(this->segment_id);
    /// 2. Get slot id.
    const uint16_t slot_id = tid.get_slot();
    const size_t page_size = buffer_manager.get_page_size();
    /// 3. Fix page.
    BufferFrame& buffer_frame = buffer_manager.fix_page(page_id, true);
    auto slotted_page = reinterpret_cast<SlottedPage*>(buffer_frame.get_page_raw_data());
    /// 4. Get slot.
    SlottedPage::Slot *const slot_to_resize = slotted_page->get_slot_ptr(slot_id);

    /// 5. Two Main Cases:
    ///             case 1: is not redirect.
    ///             case 2: is redirect.
    if (!slot_to_resize->is_redirect()) {
        /// Case 1: is not redirect.
        const auto old_size = slot_to_resize->get_size();
        auto data_offset = slot_to_resize->get_offset();
        /// 6. Init a buffer for buffering data read from slotted page.
        std::vector<std::byte> buffer;
        buffer.resize(old_size);

        /// Main Cases: is not redirect.
        if (new_size == old_size) {
            /// Trivial Case, no redirection and no change, resize to equal size.
            buffer_manager.unfix_page(buffer_frame, false);
            return;
        } else if (new_size < old_size) {
            /// Easy Case, no redirection, resize to less size, losing data at end.
            slotted_page->header.free_space += (old_size - new_size);
            slot_to_resize->set_size(new_size);
            const uint32_t free_space = slotted_page->header.free_space;
            buffer_manager.unfix_page(buffer_frame, true);
            fsi.update(page_id, free_space);
            return;
        } else if (new_size > old_size) {
            /// Hard case, resize to larger size.
            /// 7. Read (Copy) to Buffer.
            std::memcpy(buffer.data(), slotted_page->get_data() + data_offset, old_size);
            if (slotted_page->header.free_space + old_size > new_size) {
                /// This slotted_page has enough size for directly resizing, not redirection.
                ///　But still compactify if necessary, which is done by SlottedPage::relocate.
                /// 8. Relocate.
                slotted_page->relocate(slot_id, new_size, page_size);
                data_offset = slot_to_resize->get_offset();
                /// 9. Write from Buffer.
                std::memcpy(slotted_page->get_data() + data_offset, buffer.data(), old_size);
                std::memset(slotted_page->get_data() + data_offset + old_size, 0, new_size - old_size);
                /// 10. Unfix page.
                const uint32_t free_space = slotted_page->header.free_space;
                buffer_manager.unfix_page(buffer_frame, true);
                /// 11. Update FSI.
                fsi.update(page_id, free_space);
                return;
            } else {
                /// This slotted_page has NO enough size for resizing, then with redirection to a other page.
                /// 8. Search for all allocated using FSI, consider SIZE TID for this redirect target case.
                ///    allocate function updates the FSI.
                TID redirect_target_tid = allocate(new_size + sizeof(TID));
                ///     Should allocate in another page having enough space.
                assert(redirect_target_tid.get_segment_page_id() != tid.get_segment_page_id());
                assert(!slot_to_resize->is_redirect());
                slot_to_resize->set_redirect_tid(redirect_target_tid);
                assert(slot_to_resize->is_redirect());
                /// 9. Unfix the redirecting slotted page.
                slotted_page->header.free_space += old_size;
                const uint32_t free_space = slotted_page->header.free_space;
                buffer_manager.unfix_page(buffer_frame, true);
                /// 10. Update FSI.
                fsi.update(page_id, free_space);
                /// 11. Get redirect target page id.
                const page_id_t redirect_target_page_id = redirect_target_tid.get_page_id(this->table.sp_segment);
                BufferFrame& redirect_target_buffer_frame = buffer_manager.fix_page(redirect_target_page_id, true);
                /// 12. Get redirect target slot id.
                const uint16_t redirect_target_slot_id = redirect_target_tid.get_slot();
                auto redirect_target_sp_page = reinterpret_cast<SlottedPage*>(redirect_target_buffer_frame.get_page_raw_data());
                /// 13. Get redirect target slot.
                SlottedPage::Slot *redirect_target_slot = redirect_target_sp_page->get_slot_ptr(redirect_target_slot_id);
                assert(redirect_target_slot->get_size() == new_size + sizeof(TID));
                assert(!redirect_target_slot->is_redirect_target());
                redirect_target_slot->mark_as_redirect_target();
                assert(redirect_target_slot->is_redirect_target());
                /// 14. Write from Buffer.
                ///     The sizeof(TID) bytes contain the original TID.
                std::memcpy(redirect_target_sp_page->get_data() + redirect_target_slot->get_offset(), &tid, sizeof(TID));
                ///     Then the old data having old_size.
                std::memcpy(redirect_target_sp_page->get_data() + redirect_target_slot->get_offset() + sizeof(TID), buffer.data(), old_size);
                /// 15. Unfix the redirect target slotted page.
                buffer_manager.unfix_page(redirect_target_buffer_frame, true);
                /// 16. Not need to update FSI, since allocate function always updates the FSI.
                return;
            }
        }
    } else {
        /// Case 2: is redirect.
        /// Main Cases: is redirect, redirection to be resized.
        /// 7. Read redirect target TID.
        TID redirect_target_tid{slot_to_resize->as_redirect_tid()};
        /// 8. Get redirect target page id.
        const uint64_t redirect_target_page_id = redirect_target_tid.get_page_id(segment_id);
        /// 9. Get redirect target slot id.
        const uint16_t redirect_target_slot_id = redirect_target_tid.get_slot();
        /// 10. Fix page.
        BufferFrame& redirect_target_buffer_frame = buffer_manager.fix_page(redirect_target_page_id, true);
        auto const redirect_target_slotted_page = reinterpret_cast<SlottedPage*>(redirect_target_buffer_frame.get_page_raw_data());
        /// 11. Get slot.
        SlottedPage::Slot* redirect_target_slot = redirect_target_slotted_page->get_slot_ptr(redirect_target_slot_id);
        const auto redirect_target_slot_size = redirect_target_slot->get_size();
        const auto redirect_target_slot_offset = redirect_target_slot->get_offset();
        assert(redirect_target_slot->is_redirect_target());
        assert(redirect_target_slot_size > sizeof(TID));
        /// 12. Read original TID and check.
        TID original_tid{0};
        std::memcpy(&original_tid, redirect_target_slotted_page->get_data() + redirect_target_slot_offset, sizeof(TID));
        assert(original_tid.get_value() == tid.get_value());

        if (slotted_page->header.free_space >= new_size) {
            /// Resize at the redirecting page, then NO Redirection anymore.
            /// 13. Read data after original TID.
            std::vector<std::byte> buffer;
            const auto buffer_size = std::min(redirect_target_slot_size, new_size);
            buffer.resize(buffer_size);
            std::memcpy(buffer.data(), redirect_target_slotted_page->get_data() + redirect_target_slot_offset + sizeof(TID), buffer_size);
            /// erase the redirect target slot completely, so not necessary to call redirect_target_slot().
            redirect_target_slotted_page->erase(redirect_target_slot_id);
            /// 14. Unfix page := redirect_target_slotted_page.
            const uint32_t free_space_target = redirect_target_slotted_page->header.free_space;
            buffer_manager.unfix_page(redirect_target_buffer_frame, true);
            /// 15. Update FSI of redirect_target_slotted_page.
            fsi.update(redirect_target_page_id, free_space_target);
            /// 16. Not redirecting slot anymore.
            slot_to_resize->clear();
            slot_to_resize->set_offset(1);  /// mock up a non-empty slot, aka a slot with size 0. See slide 10 of chap 3.
            assert(!slot_to_resize->is_redirect());
            assert(!slot_to_resize->is_empty());  /// Not a empty slot, but is a slot with size 0. See slide 10 of chap 3.
            /// 17. Relocate at redirecting page and check correctness.
            slotted_page->relocate(slot_id, new_size, page_size);
            const auto data_offset = slot_to_resize->get_offset();
            /// 18. Copy data to inplace (relocate return always space memset-ed to 0).
            std::memcpy(slotted_page->get_data() + data_offset, buffer.data(), buffer_size);
            /// 19. Unfix page := the previous redirecting page.
            const uint32_t free_space = slotted_page->header.free_space;
            buffer_manager.unfix_page(buffer_frame, true);
            /// 20. Unfix page := the previous redirecting page.
            fsi.update(page_id, free_space);
            return;
        } else if (redirect_target_slotted_page->header.free_space + redirect_target_slot_size - sizeof(TID) >= new_size) {
            /// Redirecting page has enough space for resizing, a redirection is still need.
            /// Still resize at this redirect target page.
            /// Inplace update at redirection page, losing the data at end.
            /// 13. Unfix the redirect page (No change on this page).
            buffer_manager.unfix_page(buffer_frame, false);
            /// 14. Realocate on redirect target page.
            redirect_target_slotted_page->relocate(redirect_target_slot_id, new_size, page_size);
            const uint32_t free_space = redirect_target_slotted_page->header.free_space;
            /// 15. Unfix the redirect target page.
            buffer_manager.unfix_page(redirect_target_buffer_frame, true);
            /// 16. Update the FSI
            fsi.update(redirect_target_page_id, free_space);
            return;
        } else {
            /// Redirecting page has NO enough space for resizing, a NEW redirection is still need.
            /// OLD redirect target page is freed, to find a NEW redirect target page.
            /// So no multiple chained redirection is needed => multiple chained redirection means BAD performance.
            /// 13. Read data after original TID.
            std::vector<std::byte> buffer;
            const auto buffer_size = std::min(redirect_target_slot_size, new_size);
            buffer.resize(buffer_size);
            std::memcpy(buffer.data(), redirect_target_slotted_page->get_data() + redirect_target_slot_offset + sizeof(TID), buffer_size);
            /// erase the OLD redirect target slot completely, so not necessary to call redirect_target_slot().
            redirect_target_slotted_page->erase(redirect_target_slot_id);
            /// 14. Unfix page := OLD redirect_target_slotted_page.
            const uint32_t free_space_target = redirect_target_slotted_page->header.free_space;
            buffer_manager.unfix_page(redirect_target_buffer_frame, true);
            /// 15. Update FSI of OLD redirect_target_slotted_page.
            fsi.update(redirect_target_page_id, free_space_target);

            /// 16. Search for all allocated using FSI, consider SIZE TID for this redirect target case.
            ///    allocate function updates the FSI.
            TID new_redirect_target_tid = allocate(new_size + sizeof(TID));
            ///     Should allocate in another page having enough space.
            assert(new_redirect_target_tid.get_segment_page_id() != tid.get_segment_page_id());
            assert(new_redirect_target_tid.get_segment_page_id() != redirect_target_tid.get_segment_page_id());
            assert(slot_to_resize->is_redirect());
            slot_to_resize->set_redirect_tid(new_redirect_target_tid);
            assert(slot_to_resize->is_redirect());
            /// 17. Unfix the redirecting slotted page without changing its size (so no FSI update).
            buffer_manager.unfix_page(buffer_frame, true);

            /// 18. Get redirect target page id.
            const page_id_t new_redirect_target_page_id = new_redirect_target_tid.get_page_id(this->table.sp_segment);
            BufferFrame& new_redirect_target_buffer_frame = buffer_manager.fix_page(new_redirect_target_page_id, true);
            /// 19. Get redirect target slot id.
            const uint16_t new_redirect_target_slot_id = new_redirect_target_tid.get_slot();
            auto new_redirect_target_sp_page = reinterpret_cast<SlottedPage*>(new_redirect_target_buffer_frame.get_page_raw_data());
            /// 20. Get redirect target slot.
            SlottedPage::Slot *new_redirect_target_slot = new_redirect_target_sp_page->get_slot_ptr(new_redirect_target_slot_id);
            assert(new_redirect_target_slot->get_size() == new_size + sizeof(TID));
            assert(!new_redirect_target_slot->is_redirect_target());
            new_redirect_target_slot->mark_as_redirect_target();
            assert(new_redirect_target_slot->is_redirect_target());
            /// 21. Write from Buffer.
            ///     The sizeof(TID) bytes contain the original TID.
            std::memcpy(new_redirect_target_sp_page->get_data() + new_redirect_target_slot->get_offset(), &tid, sizeof(TID));
            ///     Then the old data having buffer_size.
            std::memcpy(new_redirect_target_sp_page->get_data() + new_redirect_target_slot->get_offset() + sizeof(TID), buffer.data(), buffer_size);
            /// 22. Unfix the redirect target slotted page.
            buffer_manager.unfix_page(new_redirect_target_buffer_frame, true);
            /// 23. Not need to update FSI, since allocate function always updates the FSI.
            return;
        }
    }
}

void SPSegment::erase(TID tid) {
    /// 1. Get page id.
    const page_id_t page_id = tid.get_page_id(segment_id);
    /// 2. Get slot id.
    const uint16_t slot_id = tid.get_slot();
    /// 3. Fix page.
    BufferFrame& buffer_frame = buffer_manager.fix_page(page_id, true);
    auto const slotted_page = reinterpret_cast<SlottedPage*>(buffer_frame.get_page_raw_data());
    /// 4. Get slot.
    SlottedPage::Slot *slot_to_erase = slotted_page->get_slot_ptr(slot_id);
    const auto slot_offset = slot_to_erase->get_offset();

    /// Cases to erase.
    if (!slot_to_erase->is_redirect()) {
        /// Case Easy: directly erase.
        /// 5. Erase.
        slotted_page->erase(slot_id);
        /// 6. Unfix page.
        const uint32_t free_space = slotted_page->header.free_space;
        buffer_manager.unfix_page(buffer_frame, true);
        /// 7. Update FSI.
        fsi.update(page_id, free_space);
    } else {
        /// Case Hard: erase redirection.
        //// 5. Read redirect target TID.
        TID redirect_target_tid{slot_to_erase->as_redirect_tid()};
        std::memcpy(&redirect_target_tid, slotted_page->get_data() + slot_offset, sizeof(TID));
        /// 6. Erase at redirecting page.
        slotted_page->erase(slot_id);
        /// 7. Unfix redirecting page.
        const uint32_t free_space = slotted_page->header.free_space;
        buffer_manager.unfix_page(buffer_frame, true);
        /// 8. Update FSI of redirecting page.
        fsi.update(page_id, free_space);
        /// 9. Get redirect target page id.
        const page_id_t redirect_target_page_id = redirect_target_tid.get_page_id(segment_id);
        /// 10. Get redirect target slot id.
        const uint16_t redirect_target_slot_id = redirect_target_tid.get_slot();
        /// 11. Fix page.
        BufferFrame& redirect_target_buffer_frame = buffer_manager.fix_page(redirect_target_page_id, true);
        auto const redirect_target_slotted_page = reinterpret_cast<SlottedPage*>(redirect_target_buffer_frame.get_page_raw_data());
        /// 12. Get slot.
        slot_to_erase = redirect_target_slotted_page->get_slot_ptr(redirect_target_slot_id);
        const auto redirect_target_slot_size = slot_to_erase->get_size();
        const auto redirect_target_slot_offset = slot_to_erase->get_offset();
        assert(slot_to_erase->is_redirect_target());
        assert(redirect_target_slot_size > sizeof(TID));
        /// 13. Read original TID and check.
        TID original_tid{0};
        std::memcpy(&original_tid, redirect_target_slotted_page->get_data() + redirect_target_slot_offset, sizeof(TID));
        assert(original_tid.get_value() == tid.get_value());
        /// 14. Erase at redirect target page.
        redirect_target_slotted_page->erase(redirect_target_slot_id);
        /// 15. Unfix page.
        const uint32_t target_free_space = redirect_target_slotted_page->header.free_space;
        buffer_manager.unfix_page(redirect_target_buffer_frame, true);
        /// 16. Update FSI of redirect target page.
        fsi.update(redirect_target_page_id, target_free_space);
    }
}
