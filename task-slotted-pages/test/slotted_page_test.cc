#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <utility>
#include <random>
#include <vector>
#include <gtest/gtest.h>
#include "slotted_page/segment.h"
#include "common/file.h"
#include "buffer/buffer_manager.h"

using SlottedPage = moderndbs::SlottedPage;

namespace {

// NOLINTNEXTLINE
TEST(SlottedPageTest, Constructor) {
    std::vector<std::byte> buffer;
    buffer.resize(1024);
    auto page = new (&buffer[0]) SlottedPage(1024);

    EXPECT_EQ(page->header.slot_count, 0);
    EXPECT_EQ(page->header.first_free_slot, 0);
    EXPECT_EQ(page->header.data_start, 1024);
    EXPECT_EQ(page->header.free_space, 1024 - sizeof(SlottedPage::Header));
}

// NOLINTNEXTLINE
TEST(SlottedPageTest, Allocation) {
    size_t page_size = 1024;
    size_t record_size = 8;
    size_t max_records = (page_size - sizeof(SlottedPage::Header)) / (record_size + sizeof(SlottedPage::Slot));

    // Prepare the slotted page
    std::vector<std::byte> buffer;
    buffer.resize(page_size);
    auto page = new (&buffer[0]) SlottedPage(page_size);

    // Allocate entry by entry
    for (size_t i = 0; i < max_records; ++i) {
        size_t slot_id = page->allocate(record_size, page_size);
        size_t expected_data_start = page_size - (i + 1) * record_size;
        size_t expected_free_space = page_size - (i + 1) * record_size
            - (i + 1) * sizeof(SlottedPage::Slot) - sizeof(SlottedPage::Header);

        ASSERT_EQ(page->header.slot_count, i + 1);
        ASSERT_EQ(page->header.first_free_slot, i + 1);
        ASSERT_EQ(page->header.data_start, expected_data_start);
        ASSERT_EQ(page->header.free_space, expected_free_space);

        auto slot = page->get_slots() + slot_id;
        ASSERT_EQ(slot->get_offset(), expected_data_start);
        ASSERT_EQ(slot->get_size(), record_size);
    }
}

// NOLINTNEXTLINE
TEST(SlottedPageTest, AllocateErase) {
    size_t page_size = 1024;
    size_t record_size = 8;

    std::vector<std::byte> buffer;
    buffer.resize(page_size);
    auto page = new (&buffer[0]) SlottedPage(page_size);
    EXPECT_EQ(page->header.slot_count, 0);
    EXPECT_EQ(page->header.first_free_slot, 0);
    EXPECT_EQ(page->header.data_start, page_size);
    EXPECT_EQ(page->header.free_space, page_size - sizeof(SlottedPage::Header));

    // Allocate a single record
    auto slot = page->allocate(record_size, page_size);
    EXPECT_EQ(page->header.slot_count, 1);
    EXPECT_EQ(page->header.first_free_slot, 1);
    EXPECT_EQ(page->header.data_start, page_size - record_size);
    EXPECT_EQ(page->header.free_space, page_size - record_size - sizeof(SlottedPage::Slot) - sizeof(SlottedPage::Header));

    // Erase a slot
    page->erase(slot);
    EXPECT_EQ(page->header.slot_count, 0);
    EXPECT_EQ(page->header.first_free_slot, 0);
    EXPECT_EQ(page->header.data_start, page_size);
    EXPECT_EQ(page->header.free_space, page_size - sizeof(SlottedPage::Header));

    // Erase a slot in between
    auto slot1 = page->allocate(record_size, page_size);
    auto slot2 = page->allocate(record_size, page_size);
    auto slot3 = page->allocate(record_size, page_size);
    page->erase(slot2);
    EXPECT_EQ(page->header.slot_count, 3);
    EXPECT_EQ(page->header.first_free_slot, 1);
    EXPECT_EQ(page->header.data_start, page_size - 3 * record_size);
    EXPECT_EQ(page->header.free_space, page_size - 2 * record_size - 3 * sizeof(SlottedPage::Slot) - sizeof(SlottedPage::Header));

    // Erase the first slot
    page->erase(slot1);
    EXPECT_EQ(page->header.slot_count, 3);
    EXPECT_EQ(page->header.first_free_slot, 0);
    EXPECT_EQ(page->header.data_start, page_size - 3 * record_size);
    EXPECT_EQ(page->header.free_space, page_size - 1 * record_size - 3 * sizeof(SlottedPage::Slot) - sizeof(SlottedPage::Header));

    // Erase the last slot
    page->erase(slot3);
    EXPECT_EQ(page->header.slot_count, 0);
    EXPECT_EQ(page->header.first_free_slot, 0);
    EXPECT_EQ(page->header.data_start, page_size - 2 * record_size);
    EXPECT_EQ(page->header.free_space, page_size - sizeof(SlottedPage::Header));
}

// NOLINTNEXTLINE
TEST(SlottedPageTest, RelocateWithoutBuffer) {
    size_t page_size = 1024;
    size_t record_size = 8;

    std::vector<std::byte> buffer;
    buffer.resize(page_size);
    auto page = new (&buffer[0]) SlottedPage(page_size);
    EXPECT_EQ(page->header.slot_count, 0);
    EXPECT_EQ(page->header.first_free_slot, 0);
    EXPECT_EQ(page->header.data_start, page_size);
    EXPECT_EQ(page->header.free_space, page_size - sizeof(SlottedPage::Header));

    page->allocate(record_size, page_size);
    auto slot2 = page->allocate(record_size, page_size);
    page->allocate(record_size, page_size);
    EXPECT_EQ(page->header.slot_count, 3);
    EXPECT_EQ(page->header.first_free_slot, 3);
    EXPECT_EQ(page->header.data_start, page_size - 3 * record_size);
    EXPECT_EQ(page->header.free_space, page_size - 3 * record_size - 3 * sizeof(SlottedPage::Slot) - sizeof(SlottedPage::Header));

    page->relocate(slot2, 2 * record_size, page_size);
    EXPECT_EQ(page->header.slot_count, 3);
    EXPECT_EQ(page->header.first_free_slot, 3);
    EXPECT_EQ(page->header.data_start, page_size - 5 * record_size);
    EXPECT_EQ(page->header.free_space, page_size - 4 * record_size - 3 * sizeof(SlottedPage::Slot) - sizeof(SlottedPage::Header));
}

// NOLINTNEXTLINE
TEST(SlottedPageTest, RelocateWithCompactification) {
    size_t page_size = 1024;
    size_t record_size = 8;
    size_t max_records = (page_size - sizeof(SlottedPage::Header)) / (record_size + sizeof(SlottedPage::Slot));

    std::vector<std::byte> buffer;
    buffer.resize(page_size);
    auto page = new (&buffer[0]) SlottedPage(page_size);
    EXPECT_EQ(page->header.slot_count, 0);
    EXPECT_EQ(page->header.first_free_slot, 0);
    EXPECT_EQ(page->header.data_start, page_size);
    EXPECT_EQ(page->header.free_space, page_size - sizeof(SlottedPage::Header));

    // Fill the page
    size_t dummyRecords = max_records - 1;
    for (size_t i = 0; i < dummyRecords; ++i) {
        page->allocate(record_size, page_size);
    }

    EXPECT_EQ(page->header.slot_count, dummyRecords);
    EXPECT_EQ(page->header.first_free_slot, dummyRecords);
    EXPECT_EQ(page->header.data_start, page_size - dummyRecords * record_size);
    EXPECT_EQ(page->header.free_space, page_size - dummyRecords * record_size - dummyRecords * sizeof(SlottedPage::Slot) - sizeof(SlottedPage::Header));
    ASSERT_EQ(page->header.free_space, 20);

    // Now relocate slot 2
    page->relocate(2, 28, page_size);

    EXPECT_EQ(page->header.slot_count, dummyRecords);
    EXPECT_EQ(page->header.first_free_slot, dummyRecords);
    EXPECT_EQ(page->header.free_space, 0);
    EXPECT_EQ(page->header.data_start, page_size - dummyRecords * record_size - 20);

    // Following added by Jigao.
    // It's implementation dependent -- just one of multiple behaviour options
    size_t expected_data_start = page_size;
    for (size_t i = 0; i < dummyRecords; ++i) {
      auto slot = page->get_slots() + i;
      if (i != 2) {
        expected_data_start -= record_size;
        ASSERT_EQ(slot->get_offset(), expected_data_start);
        ASSERT_EQ(slot->get_size(), record_size);
      }
    }
    expected_data_start -= 28;
    auto slot2 = page->get_slots() + 2;
    ASSERT_EQ(slot2->get_offset(), expected_data_start);
    ASSERT_EQ(slot2->get_size(), 28);
}

}  // namespace
