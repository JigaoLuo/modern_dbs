#ifndef INCLUDE_MODERNDBS_CONFIG_H
#define INCLUDE_MODERNDBS_CONFIG_H

#pragma once

#include <atomic>
#include <chrono>  // NOLINT
#include <cstdint>
#include <cassert>

namespace moderndbs {

using frame_id_t = int64_t;                                 // frame id type
using page_id_t = uint64_t;                                  // page id type TODO(jigao):uint64_t or int64_t => think about Slotted page id, then decide
using segment_id_t = uint16_t;                              // segment id type
using file_offset = uint64_t;                                 // offset type within segment from a page id type
using lsn_t = int32_t;                                      // log sequence number type

static_assert(sizeof(page_id_t) == 8);
static_assert(sizeof(lsn_t) == 4);

static constexpr frame_id_t INVALID_FRAME_ID = -1;          // invalid frame id
static constexpr page_id_t INVALID_PAGE_ID = -1;            // invalid page id
static constexpr size_t SIZE_PAGE_HEADER = sizeof(page_id_t) + sizeof(lsn_t);
static constexpr size_t OFFSET_PAGE_START = 0;
static constexpr size_t OFFSET_LSN = sizeof(page_id_t);

/**
 * Returns the segment id for a given page id which is contained in the 16
 * most significant bits of the page id.
 */
static constexpr segment_id_t get_segment_id(page_id_t page_id) {
  return page_id >> 48;
}

/**
 * Returns the page id within its segment for a given page id. This
 * corresponds to the 48 least significant bits of the page id.
 */
static constexpr file_offset get_segment_page_id(page_id_t page_id) {
  return page_id & ((1ull << 48) - 1);
}

// Macros to disable copying and moving
#define DISALLOW_COPY(cname)                             \
  cname(const cname &) = delete;            /* NOLINT */ \
  cname &operator=(const cname &) = delete; /* NOLINT */

#define DISALLOW_MOVE(cname)                        \
  cname(cname &&) = delete;            /* NOLINT */ \
  cname &operator=(cname &&) = delete; /* NOLINT */

#define DISALLOW_COPY_AND_MOVE(cname) \
  DISALLOW_COPY(cname);               \
  DISALLOW_MOVE(cname);

}  // namespace moderndbs


#endif //INCLUDE_MODERNDBS_CONFIG_H
