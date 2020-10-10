#ifndef INCLUDE_MODERNDBS_BUFFER_FRAME_H
#define INCLUDE_MODERNDBS_BUFFER_FRAME_H

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <shared_mutex>
#include <cstring>

#include "common/config.h"
#include "common/rwlatch.h"

namespace moderndbs {
// https://gitlab.db.in.tum.de/moderndbs-20/task-slotted-pages/-/blob/master/src/buffer_manager.cc
class BufferFrame {
    private:
    friend class BufferManager;
    std::vector<char> data;

    public:
    /// Returns a pointer to this page's data.
    inline char* get_page_raw_data()  {
      return data.data();
    }
};

// TODO(jigao): Fix bug with my buffer manager
///**
// * Buffer Frame / Page is the basic unit of storage within the database system. Buffer Frame provides a wrapper for actual data pages being
// * held in main memory. Page also contains book-keeping information that is used by the buffer pool manager, e.g.
// * pin count, dirty flag, page id, etc.
// */
//class BufferFrame {
//private:
//  /** There is book-keeping information inside the page that should only be relevant to the buffer pool manager. */
//  friend class BufferManager;
//
//  /** The id of this page. */
//  page_id_t page_id_{INVALID_PAGE_ID};
//  /** The id of the frame, where this page loaded. */
//  frame_id_t frame_id_{INVALID_FRAME_ID};
//  /** Page size*/
//  const size_t page_size;
//  /** The actual data that is stored within a page. */
//  char *const data_;
//  /** The pin count of this page. */
//  std::atomic_size_t pin_count_ {0};
//  /** Frame latch. */
//  mutable std::shared_mutex frame_latch;
//  /** True if the page is dirty, i.e. it is different from its corresponding page on disk. */
//  std::atomic_bool is_dirty_{false};
//  /** if exclusive locked */
//  std::atomic_bool exclusive_locked;
//
//  /** Zeroes out the data that is held within the page. */
//  inline void ResetMemory() { memset(data_, OFFSET_PAGE_START, page_size); }
//
//public:
//  /** Constructor. Zeros out the frame data. */
//  BufferFrame(size_t page_size, char *data_) : page_size(page_size), data_(data_) { ResetMemory(); }
//  /** Default destructor. */
//  ~BufferFrame() = default;
//  /** @return the actual raw data (page header and meta data contained) contained within this page */
//  inline char *get_page_raw_data() { return data_; }
//  /** @return the data (page header and meta data NOT contained, so 12 Bytes less) contained within this page */
////  inline char *get_page_useful_data() { return data_ + SIZE_PAGE_HEADER; }
//  /** @return the page id of this page */
//  inline page_id_t GetPageId() { return page_id_; }
//  /** @return the pin count of this page */
//  inline int GetPinCount() { return pin_count_; }
//  /** @return true if the page in memory has been modified from the page on disk, false otherwise */
//  inline bool IsDirty() { return is_dirty_; }
//  /** @return the page LSN. */
//  inline lsn_t GetLSN() { return *reinterpret_cast<lsn_t *>(data_ + OFFSET_LSN); }
//  /** Sets the page LSN. */
//  inline void SetLSN(lsn_t lsn) { memcpy(data_ + OFFSET_LSN, &lsn, sizeof(lsn_t)); }
//  /** Acquire the page write latch. */
//  inline void WLatch() { frame_latch.lock(); }
//  /** Release the page write latch. */
//  inline void WUnlatch() { frame_latch.unlock(); }
//  /** Acquire the page read latch. */
//  inline void RLatch() { frame_latch.lock_shared(); }
//  /** Release the page read latch. */
//  inline void RUnlatch() { frame_latch.unlock_shared(); }
//};
}  // namespace moderndbs
#endif //INCLUDE_MODERNDBS_BUFFER_FRAME_H
