#ifndef INCLUDE_MODERNDBS_EXTERNAL_SORT_H
#define INCLUDE_MODERNDBS_EXTERNAL_SORT_H

#pragma once

#include <cstddef>
#include <cstdint>
#include <cassert>


namespace moderndbs {

class File;

/// Sorts 64 bit unsigned integers using external sort.
/// @param[in] input      File that contains 64 bit unsigned integers which are
///                       stored as 8-byte little-endian values. This file may
///                       be in `READ` mode and should not be written to.
/// @param[in] num_values The number of integers that should be sorted from the
///                       input.
/// @param[in] output     File that should contain the sorted values in the
///                       end. This file must be in `WRITE` mode.
/// @param[in] mem_size   The maximum amount of main-memory in bytes that
///                       should be used for internal sorting.
void external_sort(File& input, size_t num_values, File& output, size_t mem_size);
}  // namespace moderndbs

#endif
