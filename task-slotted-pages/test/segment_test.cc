#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <utility>
#include <random>
#include <vector>
#include <gtest/gtest.h>
#include "buffer/buffer_manager.h"
#include "slotted_page/defer.h"
#include "common/file.h"
#include "slotted_page/hex_dump.h"
#include "slotted_page/segment.h"

using BufferManager = moderndbs::BufferManager;
using Defer = moderndbs::Defer;
using FSISegment = moderndbs::FSISegment;
using SPSegment = moderndbs::SPSegment;
using SchemaSegment = moderndbs::SchemaSegment;
using SlottedPage = moderndbs::SlottedPage;
using TID = moderndbs::TID;

namespace schema = moderndbs::schema;

namespace {

std::unique_ptr<schema::Schema> getTPCHSchemaLight() {
    std::vector<schema::Table> tables {
        schema::Table(
            "customer",
            {
                schema::Column("c_custkey", schema::Type::Integer()),
                schema::Column("c_name", schema::Type::Char(25)),
                schema::Column("c_address", schema::Type::Char(40)),
                schema::Column("c_nationkey", schema::Type::Integer()),
                schema::Column("c_phone", schema::Type::Char(15)),
                schema::Column("c_acctbal", schema::Type::Integer()),
                schema::Column("c_mktsegment", schema::Type::Char(10)),
                schema::Column("c_comment", schema::Type::Char(117)),
            },
            {
                "c_custkey"
            },
            10, 11,
            0
        ),
        schema::Table(
            "nation",
            {
                schema::Column("n_nationkey", schema::Type::Integer()),
                schema::Column("n_name", schema::Type::Char(25)),
                schema::Column("n_regionkey", schema::Type::Integer()),
                schema::Column("n_comment", schema::Type::Char(152)),
            },
            {
                "n_nationkey"
            },
            20, 21,
            0
        ),
        schema::Table(
            "region",
            {
                schema::Column("r_regionkey", schema::Type::Integer()),
                schema::Column("r_name", schema::Type::Char(25)),
                schema::Column("r_comment", schema::Type::Char(152)),
            },
            {
                "r_regionkey"
            },
            30, 31,
            0
        ),
    };
    auto schema = std::make_unique<schema::Schema>(std::move(tables));
    return schema;
}

struct SegmentTest: ::testing::Test {
    protected:

    void SetUp() override {
        using moderndbs::File;
        for (auto segment_file: std::vector<const char*>{"0", "1", "10", "11", "20", "21", "30", "31"}) {
            auto file = File::open_file(segment_file, File::Mode::WRITE);
            file->resize(0);
        }
    }
};

// NOLINTNEXTLINE
TEST_F(SegmentTest, SchemaSetter) {
    BufferManager buffer_manager(1024, 10);
    SchemaSegment schema_segment(0, buffer_manager);
    EXPECT_EQ(nullptr, schema_segment.get_schema());
    auto schema = getTPCHSchemaLight();
    auto schema_ptr = schema.get();
    schema_segment.set_schema(std::move(schema));
    EXPECT_EQ(schema_ptr, schema_segment.get_schema());
}

// NOLINTNEXTLINE
TEST_F(SegmentTest, SchemaSerialiseEmptySchema) {
    BufferManager buffer_manager(1024, 10);
    SchemaSegment schema_segment_1(0, buffer_manager);
    auto schema = std::make_unique<schema::Schema>(std::vector<schema::Table>{});
    schema_segment_1.set_schema(std::move(schema));
    schema_segment_1.write();
    SchemaSegment schema_segment_2(0, buffer_manager);
    schema_segment_2.read();
    ASSERT_NE(nullptr, schema_segment_2.get_schema());
    EXPECT_EQ(0, schema_segment_2.get_schema()->tables.size());
}

// NOLINTNEXTLINE
TEST_F(SegmentTest, SchemaSerialiseTPCHLight) {
    BufferManager buffer_manager(1024, 10);
    SchemaSegment schema_segment_1(0, buffer_manager);
    auto schema_1 = getTPCHSchemaLight();
    schema_segment_1.set_schema(std::move(schema_1));
    schema_segment_1.write();
    SchemaSegment schema_segment_2(0, buffer_manager);
    schema_segment_2.read();
    ASSERT_NE(nullptr, schema_segment_2.get_schema());
    auto schema_2 = schema_segment_2.get_schema();
    ASSERT_EQ(schema_2->tables.size(), 3);
    EXPECT_EQ(schema_2->tables[0].id, "customer");
    ASSERT_EQ(schema_2->tables[0].primary_key.size(), 1);
    EXPECT_EQ(schema_2->tables[0].primary_key[0], "c_custkey");
    ASSERT_EQ(schema_2->tables[0].columns.size(), 8);
    EXPECT_EQ(schema_2->tables[0].columns[0].id, "c_custkey");
    EXPECT_EQ(schema_2->tables[0].columns[0].type.tclass, schema::Type::Class::kInteger);
    EXPECT_EQ(schema_2->tables[0].columns[1].id, "c_name");
    EXPECT_EQ(schema_2->tables[0].columns[1].type.tclass, schema::Type::Class::kChar);
    EXPECT_EQ(schema_2->tables[0].columns[1].type.length, 25);
    EXPECT_EQ(schema_2->tables[0].columns[2].id, "c_address");
    EXPECT_EQ(schema_2->tables[0].columns[2].type.tclass, schema::Type::Class::kChar);
    EXPECT_EQ(schema_2->tables[0].columns[2].type.length, 40);
    EXPECT_EQ(schema_2->tables[0].columns[3].id, "c_nationkey");
    EXPECT_EQ(schema_2->tables[0].columns[3].type.tclass, schema::Type::Class::kInteger);
    EXPECT_EQ(schema_2->tables[0].columns[4].id, "c_phone");
    EXPECT_EQ(schema_2->tables[0].columns[4].type.tclass, schema::Type::Class::kChar);
    EXPECT_EQ(schema_2->tables[0].columns[4].type.length, 15);
    EXPECT_EQ(schema_2->tables[0].columns[5].id, "c_acctbal");
    EXPECT_EQ(schema_2->tables[0].columns[5].type.tclass, schema::Type::Class::kInteger);
    EXPECT_EQ(schema_2->tables[0].columns[6].id, "c_mktsegment");
    EXPECT_EQ(schema_2->tables[0].columns[6].type.tclass, schema::Type::Class::kChar);
    EXPECT_EQ(schema_2->tables[0].columns[6].type.length, 10);
    EXPECT_EQ(schema_2->tables[0].columns[7].id, "c_comment");
    EXPECT_EQ(schema_2->tables[0].columns[7].type.length, 117);
    EXPECT_EQ(schema_2->tables[0].columns[7].type.tclass, schema::Type::Class::kChar);
    EXPECT_EQ(schema_2->tables[1].id, "nation");
    ASSERT_EQ(schema_2->tables[1].columns.size(), 4);
    EXPECT_EQ(schema_2->tables[1].columns[0].id, "n_nationkey");
    EXPECT_EQ(schema_2->tables[1].columns[0].type.tclass, schema::Type::Class::kInteger);
    EXPECT_EQ(schema_2->tables[1].columns[1].id, "n_name");
    EXPECT_EQ(schema_2->tables[1].columns[1].type.tclass, schema::Type::Class::kChar);
    EXPECT_EQ(schema_2->tables[1].columns[1].type.length, 25);
    EXPECT_EQ(schema_2->tables[1].columns[2].id, "n_regionkey");
    EXPECT_EQ(schema_2->tables[1].columns[2].type.tclass, schema::Type::Class::kInteger);
    EXPECT_EQ(schema_2->tables[1].columns[3].id, "n_comment");
    EXPECT_EQ(schema_2->tables[1].columns[3].type.tclass, schema::Type::Class::kChar);
    EXPECT_EQ(schema_2->tables[1].columns[3].type.length, 152);
    ASSERT_EQ(schema_2->tables[1].primary_key.size(), 1);
    EXPECT_EQ(schema_2->tables[1].primary_key[0], "n_nationkey");
    EXPECT_EQ(schema_2->tables[2].id, "region");
    ASSERT_EQ(schema_2->tables[2].columns.size(), 3);
    EXPECT_EQ(schema_2->tables[2].columns[0].id, "r_regionkey");
    EXPECT_EQ(schema_2->tables[2].columns[0].type.tclass, schema::Type::Class::kInteger);
    EXPECT_EQ(schema_2->tables[2].columns[1].id, "r_name");
    EXPECT_EQ(schema_2->tables[2].columns[1].type.tclass, schema::Type::Class::kChar);
    EXPECT_EQ(schema_2->tables[2].columns[1].type.length, 25);
    EXPECT_EQ(schema_2->tables[2].columns[2].id, "r_comment");
    EXPECT_EQ(schema_2->tables[2].columns[2].type.tclass, schema::Type::Class::kChar);
    EXPECT_EQ(schema_2->tables[2].columns[2].type.length, 152);
    ASSERT_EQ(schema_2->tables[2].primary_key.size(), 1);
    EXPECT_EQ(schema_2->tables[2].primary_key[0], "r_regionkey");
}

// NOLINTNEXTLINE
TEST_F(SegmentTest, FSIEncoding) {
    BufferManager buffer_manager(1024, 10);
    auto table = schema::Table{
        "nation",
        {
            schema::Column("n_nationkey", schema::Type::Integer()),
            schema::Column("n_name", schema::Type::Char(25)),
            schema::Column("n_regionkey", schema::Type::Integer()),
            schema::Column("n_comment", schema::Type::Char(152)),
        },
        {
            "n_nationkey"
        },
        20, 21,
        0
    };
    FSISegment fsi_segment(1, buffer_manager, table);
    for (int i = 0; i < 1024; ++i) {
        auto encoded = fsi_segment.encode_free_space(i);
        auto decoded = fsi_segment.decode_free_space(encoded);
        ASSERT_LE(decoded, i) << "i=" << i << " encoded=" << std::to_string(encoded) << " decoded=" << decoded;
    }
}

// NOLINTNEXTLINE
TEST_F(SegmentTest, FSIFind) {
    BufferManager buffer_manager(1024, 10);
    SchemaSegment schema_segment(0, buffer_manager);
    schema_segment.set_schema(getTPCHSchemaLight());
    auto& table = schema_segment.get_schema()->tables[0];
    FSISegment fsi_segment(table.fsi_segment, buffer_manager, table);
    SPSegment sp_segment(table.sp_segment, buffer_manager, schema_segment, fsi_segment, table);

    auto record_size = sizeof(uint64_t);
    // Modified by Jigao: I did not use std::optional in find
    auto [found_page, page_id] = fsi_segment.find(record_size);  // NOLINT
    auto tid0 = sp_segment.allocate(record_size); // First find, then allocate

    ASSERT_TRUE(found_page);
    ASSERT_EQ(page_id, tid0.get_page_id(table.sp_segment));
}

// NOLINTNEXTLINE
TEST_F(SegmentTest, SPRecordAllocation) {
  BufferManager buffer_manager(1024, 10);
  SchemaSegment schema_segment(0, buffer_manager);
  schema_segment.set_schema(getTPCHSchemaLight());
  auto& table = schema_segment.get_schema()->tables[0];
  FSISegment fsi_segment(table.fsi_segment, buffer_manager, table);
  SPSegment sp_segment(table.sp_segment, buffer_manager, schema_segment, fsi_segment, table);
  auto max = 1024 - sizeof(SlottedPage::Slot) - sizeof(SlottedPage::Header);
  for (uint64_t i = 1; i < max; i *= 2) {
    sp_segment.allocate(i);
  }
  sp_segment.allocate(max);
}

// Modified by Jigao
// NOLINTNEXTLINE
TEST_F(SegmentTest, SPRecordAllocation_NEW) {
    BufferManager buffer_manager(1024, 10);
    SchemaSegment schema_segment(0, buffer_manager);
    schema_segment.set_schema(getTPCHSchemaLight());
    auto& table = schema_segment.get_schema()->tables[0];
    FSISegment fsi_segment(table.fsi_segment, buffer_manager, table);
    SPSegment sp_segment(table.sp_segment, buffer_manager, schema_segment, fsi_segment, table);
    auto max = 1024 - sizeof(SlottedPage::Slot) - sizeof(SlottedPage::Header);
    TID tid(0);
    for (uint64_t i = 1; i < max; i *= 2) {
      TID tid_local = sp_segment.allocate(i);
      if (i > 1) {
        // Allocate at the same slotted page or at the next slotted page
        ASSERT_LE(tid.get_segment_page_id(), tid_local.get_segment_page_id());
        if (tid.get_segment_page_id() == tid_local.get_segment_page_id()) {
          // If allocate at the same slotted page, First Come First Serve for slot id
          ASSERT_LT(tid.get_slot(), tid_local.get_slot());
        }
      }
      tid = tid_local;
    }

    // tid_max should allocate at the next new slotted page
    auto tid_max = sp_segment.allocate(max);
    ASSERT_EQ(tid.get_segment_page_id() + 1, tid_max.get_segment_page_id());

    // No enough space for allocating large block := 256 bytes
    auto [found_page, page_id] = fsi_segment.find(256);  // NOLINT
    ASSERT_FALSE(found_page);
    ASSERT_EQ(tid_max.get_page_id(sp_segment.segment_id) + 1, page_id);
}

// NOLINTNEXTLINE
TEST_F(SegmentTest, SPRecordWriteRead) {
    auto schema = getTPCHSchemaLight();
    BufferManager buffer_manager(1024, 10);
    SchemaSegment schema_segment(0, buffer_manager);
    schema_segment.set_schema(getTPCHSchemaLight());
    auto& table = schema_segment.get_schema()->tables[0];
    FSISegment fsi_segment(table.fsi_segment, buffer_manager, table);
    SPSegment sp_segment(table.sp_segment, buffer_manager, schema_segment, fsi_segment, table);
    auto max = 1024 - sizeof(SlottedPage::Slot) - sizeof(SlottedPage::Header) - sizeof(TID);

    // Sequential allocation - write - read
    std::vector<TID> tids;
    std::vector<size_t> sizes;
    std::vector<std::byte> writeBuffer;
    std::vector<std::byte> readBuffer;
    writeBuffer.resize(max);
    readBuffer.resize(max);

    // Allocate each slot, write to each slot and read from it immediately
    for (uint64_t size = 1; size < max; size *= 2) {
        auto tid = sp_segment.allocate(size);
        tids.push_back(tid);
        std::memset(writeBuffer.data(), size, size);
        sp_segment.write(tid, writeBuffer.data(), size);
        sp_segment.read(tid, readBuffer.data(), size);
        ASSERT_EQ(std::memcmp(writeBuffer.data(), readBuffer.data(), size), 0);
    }

    // Now read everything again
    int i = 0;
    for (uint64_t size = 1; size < max; size *= 2, ++i) {
        auto tid = tids[i];
        std::memset(writeBuffer.data(), size & 0xFF, size);
        sp_segment.read(tid, readBuffer.data(), size);
        ASSERT_EQ(std::memcmp(writeBuffer.data(), readBuffer.data(), size), 0);
    }
}

// NOLINTNEXTLINE
TEST_F(SegmentTest, SPRecordWriteReadRedirect) {
  BufferManager buffer_manager(1024, 10);
  SchemaSegment schema_segment(0, buffer_manager);
  schema_segment.set_schema(getTPCHSchemaLight());
  auto& table = schema_segment.get_schema()->tables[0];
  FSISegment fsi_segment(table.fsi_segment, buffer_manager, table);
  SPSegment sp_segment(table.sp_segment, buffer_manager, schema_segment, fsi_segment, table);

  auto record_size = sizeof(uint64_t);
  auto max = 1024 - sizeof(SlottedPage::Header);
  auto max_records = max / (record_size + sizeof(SlottedPage::Slot) + sizeof(TID));
  auto max_record_size = 1024 - sizeof(SlottedPage::Header) - sizeof(SlottedPage::Slot) - sizeof(TID);
  std::vector<TID> tids;
  std::vector<size_t> sizes;

  // Fill the first page
  for (uint64_t i = 0; i < max_records; ++i) {
    auto tid = sp_segment.allocate(record_size);
    tids.push_back(tid);
    sp_segment.write(tid, reinterpret_cast<std::byte*>(&i), record_size);
    uint64_t x = 0;
    sp_segment.read(tid, reinterpret_cast<std::byte*>(&x), record_size);
    ASSERT_EQ(x, i);
  }

  // Now resize a tid
  auto tid = tids.back();
  sp_segment.resize(tid, max_record_size / 2);

  // Check the first bytes of the resized record
  // If these are 0 it means that you're not copying the data when resizing.
  uint64_t x = 0;
  sp_segment.read(tid, reinterpret_cast<std::byte*>(&x), record_size);
  ASSERT_EQ(x, max_records - 1);

  // Allocate a few more pages
  for (uint64_t i = 0; i < 3 * max_records; ++i) {
    sp_segment.allocate(record_size);
  }
  sp_segment.resize(tid, max_record_size);

  // Check the record
  x = 0;
  sp_segment.read(tid, reinterpret_cast<std::byte*>(&x), record_size);
  ASSERT_EQ(x, max_records - 1);

  // Move back to original page
  sp_segment.resize(tid, record_size);

  // Resize a few more times because we can
  sp_segment.resize(tid, max_record_size);
  sp_segment.resize(tid, max_record_size / 4);
  sp_segment.resize(tid, max_record_size);
  sp_segment.resize(tid, max_record_size);
  sp_segment.resize(tid, max_record_size / 2);

  // Check the record
  x = 0;
  sp_segment.read(tid, reinterpret_cast<std::byte*>(&x), record_size);
  ASSERT_EQ(x, max_records - 1);
}

// Modified by Jigao
// NOLINTNEXTLINE
TEST_F(SegmentTest, SPRecordWriteReadRedirect_NEW) {
    BufferManager buffer_manager(1024, 10);
    SchemaSegment schema_segment(0, buffer_manager);
    schema_segment.set_schema(getTPCHSchemaLight());
    auto& table = schema_segment.get_schema()->tables[0];
    FSISegment fsi_segment(table.fsi_segment, buffer_manager, table);
    SPSegment sp_segment(table.sp_segment, buffer_manager, schema_segment, fsi_segment, table);

    auto record_size = sizeof(uint64_t);
    auto max = 1024 - sizeof(SlottedPage::Header);
    auto max_records = max / (record_size + sizeof(SlottedPage::Slot) + sizeof(TID));
    auto max_record_size = 1024 - sizeof(SlottedPage::Header) - sizeof(SlottedPage::Slot) - sizeof(TID);
    std::vector<TID> tids;
    std::vector<size_t> sizes;

    // Fill the first page
    for (uint64_t i = 0; i < max_records; ++i) {
        auto tid = sp_segment.allocate(record_size);
        tids.push_back(tid);
        sp_segment.write(tid, reinterpret_cast<std::byte*>(&i), record_size);
        uint64_t x = 0;
        sp_segment.read(tid, reinterpret_cast<std::byte*>(&x), record_size);
        ASSERT_EQ(x, i);
    }

    // Now resize a tid
    auto tid = tids.back();
    sp_segment.resize(tid, max_record_size / 2);

    // Check the first bytes of the resized record
    // If these are 0 it means that you're not copying the data when resizing.
    uint64_t x = 0;
    sp_segment.read(tid, reinterpret_cast<std::byte*>(&x), record_size);
    ASSERT_EQ(x, max_records - 1);

    // Allocate a few more pages
    for (uint64_t i = 0; i < 3 * max_records; ++i) {
        sp_segment.allocate(record_size);
    }
    sp_segment.resize(tid, max_record_size);
    // Check the record
    x = 0;
    sp_segment.read(tid, reinterpret_cast<std::byte*>(&x), record_size);
    ASSERT_EQ(x, max_records - 1);

    // Move back to original page
    sp_segment.resize(tid, record_size);
    // Check the record
    x = 0;
    sp_segment.read(tid, reinterpret_cast<std::byte*>(&x), record_size);
    ASSERT_EQ(x, max_records - 1);

    // Resize a few more times because we can
    sp_segment.resize(tid, max_record_size);
    // Check the record
    x = 0;
    sp_segment.read(tid, reinterpret_cast<std::byte*>(&x), record_size);
    ASSERT_EQ(x, max_records - 1);

    sp_segment.resize(tid, max_record_size / 4);
    // Check the record
    x = 0;
    sp_segment.read(tid, reinterpret_cast<std::byte*>(&x), record_size);
    ASSERT_EQ(x, max_records - 1);

    sp_segment.resize(tid, max_record_size);
    // Check the record
    x = 0;
    sp_segment.read(tid, reinterpret_cast<std::byte*>(&x), record_size);
    ASSERT_EQ(x, max_records - 1);

    sp_segment.resize(tid, max_record_size);
    // Check the record
    x = 0;
    sp_segment.read(tid, reinterpret_cast<std::byte*>(&x), record_size);
    ASSERT_EQ(x, max_records - 1);

    sp_segment.resize(tid, max_record_size / 2);
    // Check the record
    x = 0;
    sp_segment.read(tid, reinterpret_cast<std::byte*>(&x), record_size);
    ASSERT_EQ(x, max_records - 1);
}

// NOLINTNEXTLINE
TEST_F(SegmentTest, SPRecordErase) {
    auto schema = getTPCHSchemaLight();
    BufferManager buffer_manager(1024, 10);
    SchemaSegment schema_segment(0, buffer_manager);
    schema_segment.set_schema(getTPCHSchemaLight());
    auto& table = schema_segment.get_schema()->tables[0];
    FSISegment fsi_segment(table.fsi_segment, buffer_manager, table);
    SPSegment sp_segment(table.sp_segment, buffer_manager, schema_segment, fsi_segment, table);
    auto max = 1024 - sizeof(SlottedPage::Slot) - sizeof(SlottedPage::Header);

    // Allocate a full page
    auto tid = sp_segment.allocate(max);

    // Get the page
    auto page_id = tid.get_page_id(table.sp_segment);
    auto frame = &buffer_manager.fix_page(page_id, true);
    auto page = reinterpret_cast<SlottedPage*>(frame->get_page_raw_data());
    ASSERT_EQ(page->header.slot_count, 1);
    ASSERT_EQ(page->header.first_free_slot, 1);
    ASSERT_EQ(page->header.free_space, 0);
    ASSERT_EQ(page->header.data_start, sizeof(SlottedPage::Header) + sizeof(SlottedPage::Slot));
    buffer_manager.unfix_page(*frame, true);

    // Erase the slot
    sp_segment.erase(tid);

    // Reload the page
    page_id = tid.get_page_id(table.sp_segment);
    frame = &buffer_manager.fix_page(page_id, true);
    page = reinterpret_cast<SlottedPage*>(frame->get_page_raw_data());
    ASSERT_EQ(page->header.slot_count, 0);
    ASSERT_EQ(page->header.first_free_slot, 0);
    ASSERT_EQ(page->header.free_space, 1024 - sizeof(SlottedPage::Header));
    ASSERT_EQ(page->header.data_start, 1024);
    buffer_manager.unfix_page(*frame, true);
}

// NOLINTNEXTLINE
TEST_F(SegmentTest, SPFuzzing) {
    size_t count = 100;

    BufferManager buffer_manager(1024, 10);
    SchemaSegment schema_segment(0, buffer_manager);
    schema_segment.set_schema(getTPCHSchemaLight());
    auto& table = schema_segment.get_schema()->tables[0];
    FSISegment fsi_segment(table.fsi_segment, buffer_manager, table);
    SPSegment sp_segment(table.sp_segment, buffer_manager, schema_segment, fsi_segment, table);

    std::vector<std::vector<char>> records;
    std::vector<moderndbs::TID> tids;
    std::vector<uint16_t> lengths;

    std::mt19937_64 engine{0};
    std::chi_squared_distribution<double> chi_length(30);
    std::uniform_int_distribution<uint16_t> length_distr(1, 250);
    std::uniform_int_distribution<uint8_t> content_distr(0, 255);

    // Insert a few records.
    for (size_t i = 0; i < count; i++) {
        uint16_t rand_size = length_distr(engine);
        uint16_t rand_content = reinterpret_cast<uint8_t>(content_distr(engine));
        std::vector<char> buffer(rand_size, rand_content);

        moderndbs::TID tid = sp_segment.allocate(rand_size);
        sp_segment.write(tid, reinterpret_cast<std::byte *>(buffer.data()), buffer.size());

        lengths.push_back(rand_size);
        tids.push_back(tid);
        records.push_back(buffer);
    }

    // Check records
    // If this test fails you do not insert records correctly.
    for (size_t i = 0; i < lengths.size(); i++) {
        std::vector<char> buffer(lengths[i]);
        sp_segment.read(tids[i], reinterpret_cast<std::byte*>(buffer.data()), static_cast<uint32_t>(buffer.size()));
        ASSERT_EQ(std::equal(buffer.begin(), buffer.end(), records[i].begin()), true)
            << "[ INSERT  ] FAILED\n"
            << "[ RESIZE1 ] -\n"
            << "[ RESIZE2 ] -\n"
            << "[ ERASE   ] -\n"
            << "EXPECTED:\n"
            << moderndbs::hex_dump_str(reinterpret_cast<std::byte*>(records[i].data()), lengths[i])
            << "\nHAVE:\n"
            << moderndbs::hex_dump_str(reinterpret_cast<std::byte*>(buffer.data()), lengths[i])
            << "\n";
    }

    // Resize all records.
    for (size_t i = 0; i < count; i++) {
        uint16_t new_size = length_distr(engine);
        sp_segment.resize(tids[i], new_size);
        lengths[i] = std::min(lengths[i], new_size);
    }

    // Check records.
    // If this test fails you do not resize a non-redirected record correctly.
    for (size_t i = 0; i < lengths.size(); i++) {
        std::vector<char> buffer(lengths[i]);
        sp_segment.read(tids[i], reinterpret_cast<std::byte*>(buffer.data()), static_cast<uint32_t>(buffer.size()));
        ASSERT_EQ(std::equal(buffer.begin(), buffer.end(), records[i].begin()), true)
            << "[ INSERT  ] OK\n"
            << "[ RESIZE1 ] FAILED\n"
            << "[ RESIZE2 ] -\n"
            << "[ ERASE   ] -\n\n"
            << "EXPECTED:\n"
            << moderndbs::hex_dump_str(reinterpret_cast<std::byte*>(records[i].data()), lengths[i])
            << "\nHAVE:\n"
            << moderndbs::hex_dump_str(reinterpret_cast<std::byte*>(buffer.data()), lengths[i])
            << "\n";
    }

    // Re-Resize all records.
    for (size_t i = 0; i < count; i++) {
        uint16_t new_size = length_distr(engine);
        sp_segment.resize(tids[i], new_size);
        lengths[i] = std::min(lengths[i], new_size);
    }

    // Check records.
    // If this test fails you do not resize redirected records correctly.
    for (size_t i = 0; i < lengths.size(); i++) {
        std::vector<char> buffer(lengths[i]);
        sp_segment.read(tids[i], reinterpret_cast<std::byte*>(buffer.data()), static_cast<uint32_t>(buffer.size()));
        ASSERT_EQ(std::equal(buffer.begin(), buffer.end(), records[i].begin()), true)
            << "[ INSERT  ] OK\n"
            << "[ RESIZE1 ] OK\n"
            << "[ RESIZE2 ] FAILED\n"
            << "[ ERASE   ] -\n\n"
            << "EXPECTED:\n"
            << moderndbs::hex_dump_str(reinterpret_cast<std::byte*>(records[i].data()), lengths[i])
            << "\nHAVE:\n"
            << moderndbs::hex_dump_str(reinterpret_cast<std::byte*>(buffer.data()), lengths[i])
            << "\n";
    }

    // Erase a few records.
    uint16_t erased_records = 0;
    for (size_t i = 0; i < lengths.size(); i++) {
        if (chi_length(engine) > 45) {
            erased_records++;
            lengths.erase(lengths.begin() + i);
            sp_segment.erase(tids[i]);
            tids.erase(tids.begin() + i);
            records.erase(records.begin() + i);
            i--;
        }
    }

    // Check records.
    // If this test fails you do not erase records correctly.
    for (size_t i = 0; i < lengths.size(); i++) {
        std::vector<char> buffer(lengths[i]);
        sp_segment.read(tids[i], reinterpret_cast<std::byte*>(buffer.data()), static_cast<uint32_t>(buffer.size()));
        ASSERT_EQ(std::equal(buffer.begin(), buffer.end(), records[i].begin()), true)
            << "[ INSERT  ] OK\n"
            << "[ RESIZE1 ] OK\n"
            << "[ RESIZE2 ] OK\n"
            << "[ ERASE   ] FAILED\n\n"
            << "EXPECTED:\n"
            << moderndbs::hex_dump_str(reinterpret_cast<std::byte*>(records[i].data()), lengths[i])
            << "\nHAVE:\n"
            << moderndbs::hex_dump_str(reinterpret_cast<std::byte*>(buffer.data()), lengths[i])
            << "\n";
    }
}

}  // namespace
