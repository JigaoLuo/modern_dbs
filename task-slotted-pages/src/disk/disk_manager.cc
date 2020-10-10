#include "disk/disk_manager.h"

namespace moderndbs {

inline size_t DiskManager::GetFileSize(segment_id_t segment_id) const {
  std::shared_lock s_lock(global_latch);
  // 1. Check if the file opened in the disk manager
  //    the file must be opened, the page must be loaded
  const auto& got = file_table.find(segment_id);
  // 1.2. file must be already opened
  assert(got != file_table.end());
  SegmentFile const* segment_file = &got->second;
  // 1.3 the page size must be multiple times of page_size
  assert(segment_file->file->size() % page_size == 0);
  return segment_file->file->size();
}

void DiskManager::ReadPage(segment_id_t segment_id, file_offset offset, char *page_data) {
  SegmentFile* segment_file = nullptr;
  std::unique_lock u_lock(global_latch);
  // 1. Check if the file opened in the disk manager
  const auto& got = file_table.find(segment_id);
  if (got == file_table.end()) {
    // 1.1. file not opened yet
    // 1.1.1 open if via POSIX and insert into hash table
    segment_file = &file_table.emplace(std::piecewise_construct,
                                       std::forward_as_tuple(segment_id),
                                       std::forward_as_tuple(segment_id)).first->second;
  } else {
    // 1.2. file already opened
    segment_file = &got->second;
  }
  assert(segment_file);
  // 2.0 offset + size must not be larger than size() (precondition for read_block function call)
  //     otherwise resize(), which is not thread safe
  // resize function is not thread safe, but the constructor only called by single thread (assumed the main thread)
  if (offset * page_size + page_size > segment_file->file->size()) {
    segment_file->file_latch_.lock();
    u_lock.unlock();
    const size_t new_size = page_size * (offset + 1 + 1);
    assert(offset * page_size + page_size <= new_size);
    assert(segment_file->file->size() % page_size == 0);
    segment_file->file->resize(new_size);
    segment_file->file_latch_.unlock();
  } else {
    u_lock.unlock();
  }
  assert(offset * page_size + page_size <= segment_file->file->size());
  // 2.1 read block
  segment_file->file->read_block(offset * page_size, page_size, page_data);
}

void DiskManager::WritePage(segment_id_t segment_id, file_offset offset, char *page_data) const {
  std::shared_lock s_lock(global_latch);
  // 1. Check if the file opened in the disk manager
  //    the file must be opened, the page must be loaded
  const auto& got = file_table.find(segment_id);
  // 1.2. file must be already opened
  assert(got != file_table.end());
  SegmentFile const* segment_file = &got->second;
  // 2.0.1. file must have write mode (precondition for write_block function call)
  assert(segment_file->file->get_mode() == File::Mode::WRITE);
  // 2.0.2. offset + size must not be larger than size() (precondition for read_block function call)
  assert(offset * page_size + page_size <= segment_file->file->size());
  // 2.1. write block
  segment_file->file->write_block(page_data, offset * page_size, page_size);
}

}  // namespace moderndbs

