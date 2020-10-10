#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "slotted_page/error.h"
#include "slotted_page/schema.h"
#include "slotted_page/segment.h"
#include <cstring>
#include <limits>
#include <sstream>

using Segment = moderndbs::Segment;
using SchemaSegment = moderndbs::SchemaSegment;
using Schema = moderndbs::schema::Schema;
using Type = moderndbs::schema::Type;
using Table = moderndbs::schema::Table;
using Column = moderndbs::schema::Column;

namespace json = rapidjson;

namespace {

struct StreamBuffer: std::streambuf {
    explicit StreamBuffer(std::vector<std::byte> &buffer) {
        auto begin = reinterpret_cast<char*>(buffer.data());
        auto end = reinterpret_cast<char*>(buffer.data() + buffer.size());
        this->setg(begin, begin, end);
    }
};

const std::unordered_map<std::string, Type::Class> types {
    { "char", Type::kChar },
    { "integer", Type::kInteger },
};

}  // namespace

SchemaSegment::SchemaSegment(segment_id_t segment_id, BufferManager& buffer_manager) : Segment(segment_id, buffer_manager) {}

SchemaSegment::~SchemaSegment() {
    write();
}

void SchemaSegment::read() {
    // Load the first page
    auto& page = buffer_manager.fix_page(static_cast<uint64_t>(segment_id) << 48, false);
    auto page_data = page.get_page_raw_data();
    auto page_size = buffer_manager.get_page_size();

    // Bytes [0-8[   : Schema string length in #bytes
    // TODO(question): [0, 8[ what is that? Does SchemaSegment has its own Header?
    auto schema_size = *reinterpret_cast<uint64_t*>(page_data);

    // Read schema string into buffer
    auto remaining_bytes = schema_size;
    std::vector<std::byte> buffer;
    buffer.resize(schema_size);
    auto buffer_offset = 0;

    // Read the remainder of the first page
    // TODO(question): what is the 20 bytes? Does SchemaSegment has its own Header?
    auto d = std::min<size_t>(remaining_bytes, page_size - 20);
    std::memcpy(&buffer[buffer_offset], page_data + 20, d);
    buffer_offset += d;
    remaining_bytes -= d;

    // Release first page
    buffer_manager.unfix_page(page, false);

    // Read all other schema pages
    for (int pid = 1; remaining_bytes > 0; pid++) {
        auto &page = buffer_manager.fix_page((static_cast<uint64_t>(segment_id) << 48) ^ pid, false);

        auto page_data = page.get_page_raw_data();
        auto n = std::min<size_t>(remaining_bytes, page_size);
        std::memcpy(&buffer[buffer_offset], page_data, n);
        buffer_offset += n;
        remaining_bytes -= n;

        buffer_manager.unfix_page(page, false);
    }

    // Parse the schema
    json::Document document;
    StreamBuffer sb(buffer);
    std::istream sbi(&sb);
    json::IStreamWrapper isw(sbi);
    document.ParseStream(isw);

    // Parse the schema
    std::vector<Table> tables;
    if (document.HasMember("tables") && document["tables"].IsArray()) {
        for (auto &table : document["tables"].GetArray()) {
            auto id = table.HasMember("id") ? table["id"].GetString() : "?";
            auto sp_segment = table.HasMember("sp_segment") ? table["sp_segment"].GetInt() : -1;
            auto fsi_segment = table.HasMember("fsi_segment") ? table["fsi_segment"].GetInt() : -1;
            auto allocated_pages = table.HasMember("allocated_slotted_pages") ? table["allocated_slotted_pages"].GetInt() : -1;
            std::vector<Column> columns;
            if (table.HasMember("columns") && table["columns"].IsArray()) {
                for (auto &col : table["columns"].GetArray()) {
                    std::string id = col.HasMember("id") ? col["id"].GetString() : "?";
                    Type t;
                    if (col.HasMember("type")) {
                        auto type = col["type"].GetObject();
                        t.length = type.HasMember("length") ? type["length"].GetInt() : 0;
                        t.tclass = Type::Class::kInteger;
                        if (type.HasMember("tclass")) {
                            auto iter = types.find(type["tclass"].GetString());
                            if (iter != types.end()) {
                                t.tclass = iter->second;
                            }
                        }
                    }
                    columns.emplace_back(id, t);
                }
            }
            std::vector<std::string> primary_key;
            if (table.HasMember("primary_key") && table["primary_key"].IsArray()) {
                for (auto &pk : table["primary_key"].GetArray()) {
                    primary_key.emplace_back(pk.GetString());
                }
            }
            tables.emplace_back(id, std::move(columns), std::move(primary_key), sp_segment, fsi_segment, allocated_pages);
        }
    }
    schema = std::make_unique<Schema>(std::move(tables));
}

void SchemaSegment::write() {
    // Load the first page
    auto& page = buffer_manager.fix_page(static_cast<uint64_t>(segment_id) << 48, true);
    auto page_data = page.get_page_raw_data();
    auto page_size = buffer_manager.get_page_size();

    // Bytes [0-8[   : Schema string length in #bytes
    // TODO(question): [0, 8[ what is that? Does SchemaSegment has its own Header?
    if (!schema) {
        // Precondition: set_schema should be called, schema should be setted
        // schema is no NULL pointer, even if the table is empty
        *reinterpret_cast<uint64_t*>(page_data) = 0;
        buffer_manager.unfix_page(page, true);
        return;
    }

    // Serialize the schema to json
    json::StringBuffer buffer;
    {
        // Prepare document
        json::Document doc(json::kObjectType);
        auto &allocator = doc.GetAllocator();

        // Write tables
        json::Value tables(json::kArrayType);
        for (auto &table : schema->tables) {
            json::Value t(json::kObjectType);

            // id
            t.AddMember("id", json::StringRef(table.id.c_str()), allocator);
            // sp_segment
            t.AddMember("sp_segment", table.sp_segment, allocator);
            // fsi_segment
            t.AddMember("fsi_segment", table.fsi_segment, allocator);
            // allocated_slotted_pages
            t.AddMember("allocated_slotted_pages", table.allocated_slotted_pages, allocator);

            // Write columns
            json::Value columns(json::kArrayType);
            for (auto &col: table.columns) {
                // id
                json::Value column(json::kObjectType);
                column.AddMember("id", json::StringRef(col.id.c_str()), allocator);

                // tclass
                json::Value type(json::kObjectType);
                type.AddMember("tclass", json::StringRef(col.type.name()), allocator);

                // length
                type.AddMember("length", col.type.length, allocator);

                column.AddMember("type", type, allocator);
                columns.PushBack(column, allocator);
            }
            t.AddMember("columns", columns, allocator);

            // Write primary key
            json::Value primary_key(json::kArrayType);
            for (auto &pk: table.primary_key) {
                primary_key.PushBack(json::StringRef(pk.c_str()), allocator);
            }
            t.AddMember("primary_key", primary_key, allocator);

            tables.PushBack(t, allocator);
        }
        doc.AddMember("tables", tables, allocator);

        // Write into buffer
        json::Writer<json::StringBuffer> writer(buffer);
        doc.Accept(writer);
    }

    // Get data
    const char *json_str = buffer.GetString();
    uint64_t schema_size = buffer.GetSize();
    *reinterpret_cast<uint64_t*>(page_data) = schema_size;

    auto remaining_bytes = schema_size;
    auto buffer_offset = 0;

    // Write the remainder of the first page
    // TODO(question): what is the 20 bytes? Does SchemaSegment has its own Header?
    auto d = std::min<size_t>(remaining_bytes, page_size - 20);
    std::memcpy(page_data + 20, json_str + buffer_offset, d);
    buffer_offset += d;
    remaining_bytes -= d;

    // Release first page
    buffer_manager.unfix_page(page, true);

    // Write all other schema pages
    for (int pid = 1; remaining_bytes > 0; pid++) {
        auto &page = buffer_manager.fix_page((static_cast<uint64_t>(segment_id) << 48) ^ pid, true);

        auto page_data = page.get_page_raw_data();
        auto n = std::min<size_t>(remaining_bytes, page_size);
        std::memcpy(page_data, json_str + buffer_offset, n);
        buffer_offset += n;
        remaining_bytes -= n;

        buffer_manager.unfix_page(page, true);
    }
}
