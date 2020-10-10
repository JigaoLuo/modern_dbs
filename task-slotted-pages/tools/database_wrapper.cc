#include "slotted_page/database.h"
#include <iostream>

namespace moderndbs {
std::unique_ptr<schema::Schema> getTPCHSchemaLight() {
    std::vector<schema::Table> tables{
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
            10, 11
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
            20, 21
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
            30, 31
        ),
    };
    auto schema = std::make_unique<schema::Schema>(std::move(tables));
    return schema;
}
}  // namespace moderndbs

template<typename T>
void readLine(T& v);

template<>
void readLine(std::string& v) {
    std::getline(std::cin, v);
}

template<>
void readLine(int& v) {
    std::string line;
    std::getline(std::cin, line);
    try {
        v = std::stoi(line);
    }
    catch (...) {}
}

int main() {
    auto db = moderndbs::Database();

    int choice = 0;
    do {
        std::cout << "(1) Load schema from segment\n";
        std::cout << "(2) Load TPCH-like schema\n";
        std::cout << "> " << std::flush;
        readLine(choice);
    } while (choice != 1 && choice != 2);
    if (choice == 1) {
        // only support schema in segment 0 for now
        db.load_schema(0);
    } else if (choice == 2) {
        db.load_new_schema(moderndbs::getTPCHSchemaLight());
    }

    while (true) {
        do {
            std::cout << "(1) insert\n";
            std::cout << "(2) read\n";
            std::cout << "> " << std::flush;
            readLine(choice);
        } while (choice != 1 && choice != 2);
        if (choice == 1) {
            do {
                std::cout << "Select table:\n";
                for (size_t i = 0; i < db.get_schema().tables.size(); ++i) {
                    std::cout << "(" << i << ") " << db.get_schema().tables[i].id << "\n";
                }
                std::cout << "> " << std::flush;
                readLine(choice);
            } while (choice < 0 || size_t(choice) >= db.get_schema().tables.size());
            auto &table = db.get_schema().tables[choice];
            auto values = std::vector<std::string>();
            for (auto &column : table.columns) {
                std::cout << "Value for " << column.id << "(" << column.type.name() << ")";
                std::cout << " > " << std::flush;
                std::string value;
                readLine(value);
                values.emplace_back(move(value));
            }
            db.insert(table, values);
        } else if (choice == 2) {
            do {
                std::cout << "Select table:\n";
                for (size_t i = 0; i < db.get_schema().tables.size(); ++i) {
                    std::cout << "(" << i << ") " << db.get_schema().tables[i].id << "\n";
                }
                std::cout << "> " << std::flush;
                readLine(choice);
            } while (choice < 0 || size_t(choice) >= db.get_schema().tables.size());
            auto &table = db.get_schema().tables[choice];
            std::cout << "Enter TID:\n";
            std::cout << "> " << std::flush;
            readLine(choice);
            db.read_tuple(table, moderndbs::TID(choice));
        }
    }
}