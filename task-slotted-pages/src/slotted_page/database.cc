#include "slotted_page/database.h"

void moderndbs::Database::insert(const moderndbs::schema::Table &table,
                                 const std::vector<std::string> &data) {
    if (table.columns.size() != data.size()) {
        throw std::runtime_error("invalid data");
    }

    // Serialize the data
    auto insert_buffer = std::vector<char>();
    for (size_t i = 0; i < data.size(); ++i) {
        auto &column = table.columns[i];
        auto &s = data[i];

        int integer;
        switch (column.type.tclass) {
            case schema::Type::Class::kInteger:
                integer = atoi(s.c_str());
                for (size_t j = 0; j < sizeof(integer); ++j) {
                    insert_buffer.push_back(reinterpret_cast<char*>(&integer)[j]);
                }
                break;
            case schema::Type::Class::kChar:
                for (size_t j = 0; j < column.type.length; ++j) {
                    if (j < s.size()) {
                        insert_buffer.push_back(s[j]);
                    } else {
                        insert_buffer.push_back(' ');
                    }
                }
                break;
        }
    }

    SPSegment &sp = *slotted_pages.at(table.sp_segment);
    auto tid = sp.allocate(insert_buffer.size());
    sp.write(tid, reinterpret_cast<std::byte*>(insert_buffer.data()), insert_buffer.size());
    std::cout << "Tuple with TID " << tid.get_value() << " inserted!\n";
}

void moderndbs::Database::load_new_schema(std::unique_ptr<moderndbs::schema::Schema> schema) {
    if (schema_segment) {
        schema_segment->write();
    }
    // Always load it to segmentID 0, should be good enough for now
    schema_segment = std::make_unique<SchemaSegment>(0, buffer_manager);
    schema_segment->set_schema(std::move(schema));
    for (auto& table : schema_segment->get_schema()->tables) {
        free_space_inventory.emplace(table.fsi_segment, std::make_unique<FSISegment>(table.fsi_segment, buffer_manager, table));
        slotted_pages.emplace(table.sp_segment, std::make_unique<SPSegment>(table.sp_segment, buffer_manager, *schema_segment, *free_space_inventory.at(table.fsi_segment), table));
    }
}

moderndbs::schema::Schema &moderndbs::Database::get_schema() {
    return *schema_segment->get_schema();
}

void moderndbs::Database::read_tuple(const moderndbs::schema::Table &table, moderndbs::TID tid) {
    auto& sps = *slotted_pages.at(table.sp_segment);
    auto read_buffer = std::vector<char>(1024);
    auto read_bytes = sps.read(tid, reinterpret_cast<std::byte*>(read_buffer.data()), read_buffer.size());
    // Deserialize the data
    char* current = read_buffer.data();
    for (auto &column : table.columns) {
        int integer;
        switch (column.type.tclass) {
            case schema::Type::Class::kInteger:
                integer = *reinterpret_cast<int*>(current);
                std::cout << integer;
                current += sizeof(int);
                break;
            case schema::Type::Class::kChar:
                for (size_t j = 0; j < column.type.length; ++j) {
                    std::cout << *current;
                    ++current;
                }
                break;
        }
        if (std::distance(read_buffer.data(), current) > read_bytes) {
            break;
        }
        std::cout << " | ";
    }
    std::cout << std::endl;
}

void moderndbs::Database::load_schema(int16_t schema) {
    if (schema_segment) {
        schema_segment->write();
    }
    schema_segment = std::make_unique<SchemaSegment>(schema, buffer_manager);
    schema_segment->read();
}

