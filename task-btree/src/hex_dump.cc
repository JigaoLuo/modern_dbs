// ---------------------------------------------------------------------------------------------------
// MODERNDBS
// ---------------------------------------------------------------------------------------------------

#include "moderndbs/hex_dump.h"
#include <iomanip>

void moderndbs::hex_dump(const std::byte* data, size_t length, std::ostream& out, std::size_t width) {
    auto begin = reinterpret_cast<const char*>(data);
    auto end = begin + length;
    size_t line_length = 0;
    for (auto line = begin; line != end; line += line_length) {
        out.width(4);
        out.fill('0');
        out << std::hex << line - begin << " : ";
        line_length = std::min(width, static_cast<std::size_t>(end - line));
        for (std::size_t pass = 1; pass <= 2; ++pass) {
            for (const char* next = line; next != end && next != line + width; ++next) {
                char ch = *next;
                switch(pass) {
                    case 1:
                        out << (ch < 32 ? '.' : ch);
                        break;
                    case 2:
                        if (next != line) {
                            out << " ";
                        }
                        out.width(2);
                        out.fill('0');
                        out << std::hex << std::uppercase << static_cast<int>(static_cast<unsigned char>(ch));
                        break;
                }
            }
            if (pass == 1 && line_length != width) {
                out << std::string(width - line_length, ' ');
            }
            out << " ";
        }
        out << std::endl;
    }
}

std::string moderndbs::hex_dump_str(const std::byte* data, size_t length, std::size_t width) {
    std::stringstream out;
    hex_dump(data, length, out, width);
    return out.str();
}
