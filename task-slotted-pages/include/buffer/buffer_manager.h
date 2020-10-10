#ifndef INCLUDE_MODERNDBS_BUFFER_MANAGER_H
#define INCLUDE_MODERNDBS_BUFFER_MANAGER_H

#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <vector>
#include <memory>
#include <unordered_map>

#include "buffer/2Q_replacer.h"
#include "buffer/buffer_frame.h"
#include "disk/disk_manager.h"

namespace moderndbs {

class buffer_full_error
: public std::exception {
public:
    const char* what() const noexcept override {
        return "buffer is full with all pages pinned";
    }
};

// https://gitlab.db.in.tum.de/moderndbs-20/task-slotted-pages/-/blob/master/src/buffer_manager.cc
class BufferManager {
private:
  size_t page_size;
  std::unordered_map<uint64_t, BufferFrame> pages;

public:
  /// Constructor.
  /// @param[in] page_size  Size in bytes that all pages will have.
  /// @param[in] page_count Maximum number of pages that should reside in
  //                        memory at the same time.
  BufferManager(size_t page_size, size_t page_count);

  /// Destructor. Writes all dirty pages to disk.
  ~BufferManager();

  /// Return page size with raw data (page header and meta data contained)
  size_t get_page_size() const { return page_size; }

  /// Returns a reference to a `BufferFrame` object for a given page id. When
  /// the page is not loaded into memory, it is read from disk. Otherwise the
  /// loaded page is used.
  /// When the page cannot be loaded because the buffer is full, throws the
  /// exception `buffer_full_error`.
  /// Is thread-safe w.r.t. other concurrent calls to `fix_page()` and
  /// `unfix_page()`.
  /// @param[in] page_id   Page id of the page that should be loaded.
  /// @param[in] exclusive If `exclusive` is true, the page is locked
  ///                      exclusively. Otherwise it is locked
  ///                      non-exclusively (shared).
  BufferFrame& fix_page(uint64_t page_id, bool exclusive);

  /// Takes a `BufferFrame` reference that was returned by an earlier call to
  /// `fix_page()` and unfixes it. When `is_dirty` is / true, the page is
  /// written back to disk eventually.
  void unfix_page(BufferFrame& page, bool is_dirty);
};

// TODO(jigao): Fix bug with my buffer manager
//class BufferManager {
//private:
//  /** size of page */
//  const size_t page_size_;  // NOLINT
//  /** size of frames in buffer pool */
//  const size_t page_count_;
//  /** pointer to replacer to find unpinned pages for replacement. TwoQReplacer is thread safe*/
//  std::unique_ptr<TwoQReplacer> replacer_;
//  /** list of free pages. */
//  std::list<frame_id_t> free_list_;
//  /** pointer to the disk manager. DiskManager is thread safe */
//  std::unique_ptr<DiskManager> disk_manager_;
//  /** pointer to buffer pool = only for page's data_ */
//  std::unique_ptr<char[]> buffer_pool_;
//  /** vector of buffer frames = metadata of pages. Each buffer frame has its latch */
//  std::vector<std::shared_ptr<BufferFrame>> buffer_frames_;
//  /** page table as a hash table: key := page_id, value := frame_id = index in buffer_frames */
//  std::unordered_map<page_id_t, frame_id_t> page_table_;
//  /** global latch for buffer manager := for buffer_pool_, page_table_*/
//  mutable std::shared_mutex global_latch_;
//
//public:
//  /// Constructor.
//  /// @param[in] page_size  Size in bytes that all pages will have.
//  /// @param[in] page_count Maximum number of pages that should reside in
//  ///                       memory at the same time.
//  BufferManager(size_t page_size, size_t page_count);
//
//  /// Destructor. Writes all dirty pages to disk.
//  ~BufferManager();
//
//  /// Returns a reference to a `BufferFrame` object for a given page id. When
//  /// the page is not loaded into memory, it is read from disk. Otherwise the
//  /// loaded page is used.
//  /// When the page cannot be loaded because the buffer is full, throws the
//  /// exception `buffer_full_error`.
//  /// Is thread-safe w.r.t. other concurrent calls to `fix_page()` and
//  /// `unfix_page()`.
//  /// @param[in] page_id   Page id of the page that should be loaded.
//  /// @param[in] exclusive If `exclusive` is true, the page is locked
//  ///                      exclusively. Otherwise it is locked
//  ///                      non-exclusively (shared).
//  BufferFrame& fix_page(page_id_t page_id, bool exclusive);
//
//  /// Takes a `BufferFrame` reference that was returned by an earlier call to
//  /// `fix_page()` and unfixes it. When `is_dirty` is / true, the page is
//  /// written back to disk eventually.
//  void unfix_page(BufferFrame& page, bool is_dirty);
//
//  /// Returns the page ids of all pages (fixed and unfixed) that are in the
//  /// FIFO list in FIFO order.
//  /// Is not thread-safe.
//  std::vector<uint64_t> get_fifo_list() const;
//
//  /// Returns the page ids of all pages (fixed and unfixed) that are in the
//  /// LRU list in LRU order.
//  /// Is not thread-safe.
//  std::vector<uint64_t> get_lru_list() const;
//
//  /// Return page size with raw data (page header and meta data contained)
//  size_t get_page_size() const { return page_size_; }
//
//  /// Return page size without raw data (page header and meta data contained)
////  size_t get_useful_page_size() const { return page_size_ - SIZE_PAGE_HEADER; }
//};


}  // namespace moderndbs

#endif
