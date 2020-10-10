#ifndef INCLUDE_MODERNDBS_DISK_MANAGER_H
#define INCLUDE_MODERNDBS_DISK_MANAGER_H

/**
 * disk_manager.h
 *
 * Disk manager takes care of multiple accessed files within a
 * database. It also performs read and write of pages to and from disk, and
 * provides a logical file layer within the context of a database management
 * system.
 */

#pragma once

#include <atomic>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <memory>
#include <unordered_map>

#include "common/config.h"
#include "common/file.h"
#include "buffer/buffer_frame.h"

namespace moderndbs {

struct SegmentFile {
  /** only for the resize function, which is not thread safe*/
  mutable std::shared_mutex file_latch_;
  /** segment id */
  const segment_id_t segment_id;
  /** file name */
  const std::string file_name;
  /** point to opened file */
  std::unique_ptr<File> file;

  explicit SegmentFile(segment_id_t segment_id)
    : segment_id(segment_id), file_name(std::to_string(segment_id)), file(File::open_file(file_name.c_str(), File::Mode::WRITE)) {};
  ~SegmentFile() = default;

  DISALLOW_COPY_AND_MOVE(SegmentFile);
};

class DiskManager {
public:
  explicit DiskManager(size_t page_size) : page_size(page_size) {};

  ~DiskManager() = default;

  /** check if file with segment id already opened */
  bool IfFileOpened(segment_id_t segment_id) const {
    std::shared_lock s_lock(global_latch);
    return file_table.find(segment_id) != file_table.end();
  }

  size_t GetPageSize() const {
    return page_size;
  }

  /** get size of a opened file having segment_id */
  inline size_t GetFileSize(segment_id_t segment_id) const;

  /**
   * Read the contents of the specified page into the given memory area
   * precondition: page latch is locked
   */
  void ReadPage(segment_id_t segment_id, file_offset offset, char *page_data);

  /**
   * Write the contents of the specified page into disk file
   * precondition: page latch is locked
   */
  void WritePage(segment_id_t segment_id, file_offset offset, char *page_dataframe_id_) const;

private:
  /** page size*/
  const size_t page_size;
  /** hash table */
  std::unordered_map<segment_id_t, SegmentFile> file_table;
  /** global latch protecting hash table */
  mutable std::shared_mutex global_latch;
};

} // namespace moderndbs

#endif //INCLUDE_MODERNDBS_DISK_MANAGER_H
