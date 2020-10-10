#ifndef INCLUDE_MODERNDBS_SLOTTED_PAGE_H_
#define INCLUDE_MODERNDBS_SLOTTED_PAGE_H_

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace moderndbs {
struct TID {
    /**
     *  TID format as uint64_t:
     *       0x  000000000000       '   0000
     * page_id without segment id   |  slot id
     *              48bit               16bit
     */
    public:
    /// Constructor.
    explicit TID(uint64_t raw_value) : value{raw_value} {}

    /// Constructor.
    TID(uint64_t page, uint16_t slot) : value{(page << 16) ^ (slot & 0xFFFF)} {}

    /// Get buffer page id.
    uint64_t get_page_id(uint16_t segment_id) { return (value >> 16) ^ (static_cast<uint64_t>(segment_id) << 48); }

    /// Get page_id without segment id  .
    uint64_t get_segment_page_id() { return value >> 16; }

    /// Get the slot.
    uint16_t get_slot() { return value & 0xFFFF; }

    /// Get the value.
    uint64_t get_value() { return value; }

    private:
    /// The TID value.
    uint64_t value;
};

struct SlottedPage {
    /**
     * Slotted page format -- single line:
     * -------------------------------------------------------------------------
     * | HEADER | ... SLOTS ... | ... FREE SPACE ... | ... INSERTED TUPLES ... |
     * -------------------------------------------------------------------------
     *          ^                                                              ^
     *       12 Bytes                                                     page_size Bytes
     *
     *
     *
     *
     * Slotted page format -- multiple line:
     *
     * --------------------------------------------------------------------------------
     * | slot_count | first_free_slot | data_start | free_space |  Slot 0  |  Slot 1  |
     * --------------------------------------------------------------------------------
     *                                                          ^
     *                                                  Header := 12 Bytes
     * --------------------------------------------------------------------------------
     * |  Slot 2  |  No Slot  |  Slot 3  |  No Slot  |  Slot 4  | FREE SPACE | Data 4 |
     * --------------------------------------------------------------------------------
     *            ^                                                          ^
     *       first_free_slot                                              data_start
     * -------------------------------------
     * | Data 3 | Data 2 | Data 1 | Data 0 |
     * -------------------------------------
     *                                     ^
     *                                page_size Bytes
     *
     *
     *
     *  Slot format as uint64_t: (cf Slide 13 of Chap 3)
     *     0x    FF           '            00               '     000000    '   000000
     *   T:redirect indicator | S:redirect target indicator |    O:offset   |   S:size
     *            56bit               48bit           24bit
     */
    struct Header {
        /// Constructor
        explicit Header(uint32_t page_size) : slot_count{0}, first_free_slot{0}, data_start{page_size}, free_space{static_cast<uint32_t>(page_size - sizeof(Header))} {} ;

        /// Number of currently used slots.
        uint16_t slot_count;
        /// To speed up the search for a free slot. To amortize the overhead to iterate all slots to find the first free one, which is costly.
        uint16_t first_free_slot;
        /// Lower end of the data.
        uint32_t data_start;
        /// Space that would be available after compactification.
        uint32_t free_space;
    };

    struct Slot {
        /// Constructor
        Slot() : value{EMPTY_NON_REDIRECT_SLOT} {}

        /// Is redirect?
        /// T:redirect indicator:
        /// The first byte is NOT 0xFF, then it is redirected and stored a TID in slot (so not a slot any more). The TID -> another page having the redirect target record.
        /// The consequence of that: TID can't start with 0xFF, which is a precondition of TID.
        bool is_redirect() const { return (value >> 56) != 0xFF; }

        /// Is redirect target?
        /// S:redirect target indicator:
        bool is_redirect_target() const { return ((value >> 48) & 0xFF) != 0; }

        /// Reinterpret slot as TID.
        TID as_redirect_tid() const { return TID(value); }

        /// Clear the slot.
        void clear() { value = EMPTY_NON_REDIRECT_SLOT; }

        /// Get the size.
        uint32_t get_size() const { return value & 0xFFFFFFull; }

        /// Get the offset.
        uint32_t get_offset() const { return (value >> 24) & 0xFFFFFFull; }

        /// Is empty?
        /// Empty := is not redirect AND size == 0 AND offset == 0
        bool is_empty() const { return value == EMPTY_NON_REDIRECT_SLOT; }

        /// Set the offset.
        void set_offset(uint32_t offset) { value = (value & UNSET_OFFSET_MASK) ^ (static_cast<uint64_t>(offset) << 24); }

        /// Set the size.
        void set_size(uint32_t size) { value = (value & UNSET_SIZE_MASK) ^ (static_cast<uint64_t>(size)); }

        /// Set the slot.
        void set_slot(uint32_t offset, uint32_t size, bool is_redirect_target = false) {
            value = 0;
            value ^= size & 0xFFFFFFull;
            value ^= (offset & 0xFFFFFFull) << 24;
            value ^= (is_redirect_target ? 0xFFull : 0x00ull) << 48;
            value ^= 0xFFull << 56;
        }

        /// Set the redirect.
        void set_redirect_tid(TID tid) {
            assert((0xFF - (tid.get_value() >> 56)) > 0 && "invalid tid");
            value = tid.get_value();
        }

        /// Mark a slot as redirect target.
        void mark_as_redirect_target(bool redirect_target = true) {
            value &= ~(0xFFull << 48);
            if (redirect_target) {
                value ^= 0xFFull << 48;
            }
        }

        /// The slot value.
        uint64_t value{EMPTY_NON_REDIRECT_SLOT};
        static constexpr uint64_t EMPTY_NON_REDIRECT_SLOT = 0xFF'00'000000'000000;
        static constexpr uint64_t UNSET_OFFSET_MASK       = 0xFF'FF'000000'FFFFFF;
        static constexpr uint64_t UNSET_SIZE_MASK         = 0xFF'FF'FFFFFF'000000;
    };

    /// Constructor.
    /// @param[in] page_size    The size of a buffer frame.
    explicit SlottedPage(uint32_t page_size) : header{page_size} { std::memset(get_data() + sizeof(Header), 0x00, page_size - sizeof(Header)); }

    /// Get data.
    std::byte *get_data() { return reinterpret_cast<std::byte*>(this); }
    /// Get constant data.
    const std::byte *get_data() const { return reinterpret_cast<const std::byte*>(this); }

    /// Get slots.
    Slot *get_slots() { return reinterpret_cast<Slot*>(get_data() + sizeof(Header)); }
    /// Get constant slots.
    const Slot *get_slots() const { return reinterpret_cast<const Slot*>(get_data() + sizeof(Header)); }

    /// Get a slot as pointer with slot_id
    Slot *get_slot_ptr(uint16_t slot_id) { return reinterpret_cast<Slot*>(reinterpret_cast<std::byte*>(this) + sizeof(Header) + (slot_id * sizeof(Slot))); }

    /// Get the compacted free space.
    uint32_t get_free_space() { return header.free_space; }

    /// Allocate a slot. Only in current slotted page, no redirection.
    /// @param[in] data_size    The slot that should be allocated
    /// @param[in] page_size    The new size of a slot
    ///                              for only tuple data, no slot size considered
    uint16_t allocate(uint32_t data_size, uint32_t page_size);

    /// Relocate a slot. Only in current slotted page, no redirection.
    /// @param[in] slot_id      The slot that should be relocated
    /// @param[in] data_size    The new size of a slot
    /// @param[in] page_size    The size of the page
    void relocate(uint16_t slot_id, uint32_t data_size, uint32_t page_size);

    /// Erase a slot.
    /// @param[in] slot_id      The slot that should be erased
    void erase(uint16_t slot_id);

    /// The header.
    /// Note that the slotted page itself should reside on the buffer frame!
    /// DO NOT allocate heap objects for a slotted page but instead reinterpret_cast BufferFrame.get_data()!
    /// This is also the reason why the constructor and compactify require the actual page size as argument.
    /// (The slotted page itself does not know how large it is)
    Header header;

    private:
    /// Set a slot with slot_id
    void set_slot(uint16_t slot_id, uint32_t data_offset, uint32_t data_size, bool is_redirect_target = false) {
        reinterpret_cast<Slot*>(reinterpret_cast<std::byte*>(this) + sizeof(Header) + (slot_id * sizeof(Slot)))->set_slot(data_offset, data_size, is_redirect_target);
    }

    /// Erase a slot with slot_id
    void clear_slot(uint16_t slot_id) { reinterpret_cast<Slot*>(reinterpret_cast<std::byte*>(this) + sizeof(Header) + (slot_id * sizeof(Slot)))->clear(); }

    /// Check a slot if is empty
    bool is_empty_slot(uint16_t slot_id) { return reinterpret_cast<Slot*>(reinterpret_cast<std::byte*>(this) + sizeof(Header) + (slot_id * sizeof(Slot)))->is_empty(); }

    /// Compact the page.
    /// @param[in] page_size    The size of a buffer frame.
    void compactify(uint32_t page_size);

    /// Get the un-fragmented free space := the continuous memory between last slot and last data.
    uint32_t get_continuous_free_space();
};
static_assert(sizeof(SlottedPage) == sizeof(SlottedPage::Header), "An empty slotted page must only contain the header");
}  // namespace moderndbs

#endif // INCLUDE_MODERNDBS_SLOTTED_PAGE_H_
