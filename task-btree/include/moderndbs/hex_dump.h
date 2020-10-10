#ifndef INCLUDE_MODERNDBS_HEX_DUMP_H
#define INCLUDE_MODERNDBS_HEX_DUMP_H

#include <sstream>
#include <cstddef>

namespace moderndbs {

/// Dump a buffer to an ostream.
/// @param[in] data       The data that should be dumped
/// @param[in] length     The length of the data
/// @param[in] out        The output stream
/// @param[in] width      The line width
void hex_dump(const std::byte* data, size_t length, std::ostream& out, std::size_t width = 16);

/// Dump a buffer to a string.
/// @param[in] data       The data that should be dumped
/// @param[in] length     The length of the data
/// @param[in] width      The line width
/// @return               The hex dump string
std::string hex_dump_str(const std::byte* data, size_t length, std::size_t width = 16);

}  // namespace moderndbs

#endif
