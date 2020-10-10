#include "buffer/buffer_manager.h"


namespace moderndbs {

// https://gitlab.db.in.tum.de/moderndbs-20/task-slotted-pages/-/blob/master/src/buffer_manager.cc
BufferManager::BufferManager(size_t page_size, size_t /*page_count*/) : page_size(page_size) {
}


BufferManager::~BufferManager() = default;


BufferFrame& BufferManager::fix_page(uint64_t page_id, bool /*exclusive*/) {
  auto result = pages.emplace(page_id, BufferFrame{});
  auto& page = result.first->second;
  bool is_new = result.second;
  if (is_new) {
    page.data.resize(page_size, 0);
  }
  return page;
}


void BufferManager::unfix_page(BufferFrame& /*page*/, bool /*is_dirty*/) {
}

// TODO(jigao): Fix bug with my buffer manager
//BufferManager::BufferManager(size_t page_size, size_t page_count)
//: page_size_(page_size), page_count_(page_count), replacer_(std::make_unique<TwoQReplacer>(page_count)),
//  disk_manager_(std::make_unique<DiskManager>(page_size)), buffer_pool_(std::make_unique<char[]>(page_count * page_size)) {
//  buffer_frames_.reserve(page_count);
//  for (size_t i = 0; i < page_count; i++) {
//    buffer_frames_.emplace_back(std::make_shared<BufferFrame>(page_size, buffer_pool_.get() + i * page_size));
//    free_list_.emplace_back(static_cast<frame_id_t >(i));
//  }
//}
//
//BufferManager::~BufferManager() {  // NOLINT
//  // flush all dirty pages to disk
//  for (size_t i = 0; i < page_count_; i++) {
//    auto& buffer_frame = buffer_frames_[i];
//    if (buffer_frame->page_id_ != INVALID_PAGE_ID && buffer_frame->frame_id_ != INVALID_FRAME_ID && buffer_frame->is_dirty_) {
//      const segment_id_t segment_id = get_segment_id(buffer_frame->page_id_);
//      const file_offset offset = get_segment_page_id(buffer_frame->page_id_);
//      disk_manager_->WritePage(segment_id, offset, buffer_frame->data_);
//    }
//  }
//}
//
//BufferFrame& BufferManager::fix_page(page_id_t page_id, bool exclusive) {
//  assert(page_id != INVALID_PAGE_ID);
//  frame_id_t frame_id;
//  std::shared_ptr<BufferFrame> buffer_frame;
//  const segment_id_t segment_id = get_segment_id(page_id);
//  const file_offset offset = get_segment_page_id(page_id);
//  std::unique_lock u_lock(global_latch_);
//  assert(buffer_frames_.size() == page_count_);
//  // 1. look up in page table
//  const auto& got = page_table_.find(page_id);
//  if (got == page_table_.end()) {
//    // 1.1 not loaded in buffer
//    if (!free_list_.empty()) {
//      // 1.1.1. first try to get from free list
//      frame_id = free_list_.front();
//      free_list_.pop_front();
//      page_table_.emplace(page_id, frame_id);
//      buffer_frame = buffer_frames_[frame_id];
//      assert(buffer_frame->pin_count_ == 0);
//      assert(buffer_frame->page_id_ == INVALID_PAGE_ID);
//      assert(buffer_frame->frame_id_ == INVALID_FRAME_ID);
//      assert(buffer_frame->is_dirty_ == false);
//      buffer_frame->page_id_ = page_id;
//      disk_manager_->ReadPage(segment_id, offset, buffer_frame->data_);
//    } else if (replacer_->Size() != 0) {
//      // 1.1.2. then try to get from replacer
//      auto victim_res = replacer_->Victim(&frame_id);
//      assert(victim_res);
//      page_table_.emplace(page_id, frame_id);
//      buffer_frame = buffer_frames_[frame_id];
//      page_table_.erase(buffer_frame->page_id_);
//      assert(buffer_frame->pin_count_ == 0);
//      assert(buffer_frame->page_id_ != INVALID_PAGE_ID);
//      assert(buffer_frame->frame_id_ != INVALID_FRAME_ID);
//      assert(buffer_frame->frame_id_ == frame_id);
//      if (buffer_frame->is_dirty_) {
//        const segment_id_t old_segment_id = get_segment_id(buffer_frame->page_id_);
//        const file_offset old_offset = get_segment_page_id(buffer_frame->page_id_);
//        disk_manager_->WritePage(old_segment_id, old_offset, buffer_frame->data_);
//        buffer_frame->is_dirty_ = false;
//      }
//      buffer_frame->page_id_ = page_id;
//      disk_manager_->ReadPage(segment_id, offset, buffer_frame->data_);
//    } else {
//      // 1.1.3. all pages are pinned/fix
//      throw buffer_full_error{};
//    }
//    buffer_frame->pin_count_ = 1;
//    buffer_frame->frame_id_ = frame_id;
//    buffer_frame->exclusive_locked = exclusive;
//    exclusive ? buffer_frame->WLatch() : buffer_frame->RLatch();
//    return *buffer_frame;
//  } else {
//    // 1.2 already loaded in buffer
//    frame_id = got->second;
//    buffer_frame = buffer_frames_[frame_id];
//    replacer_->Pin(frame_id);
//    assert(buffer_frame->frame_id_ == frame_id);
//    buffer_frame->pin_count_++;
//    buffer_frame->frame_id_ = frame_id;
//    buffer_frame->exclusive_locked = exclusive;
//    exclusive ? buffer_frame->WLatch() : buffer_frame->RLatch();
//    return *buffer_frame;
//  }
//}
//
//void BufferManager::unfix_page(BufferFrame& page, bool is_dirty) {
//  assert(page.page_id_ != INVALID_PAGE_ID);
//  assert(page.frame_id_ != INVALID_FRAME_ID);
//  assert(page.pin_count_ > 0);
//  page.is_dirty_ = is_dirty;
//  if (--page.pin_count_ == 0) {
//    replacer_->Unpin(page.frame_id_);
//  }
//  page.exclusive_locked ? page.WUnlatch() : page.RUnlatch();
//}
//
//std::vector<uint64_t> BufferManager::get_fifo_list() const {
//  std::vector<frame_id_t> frame_id_fifo = replacer_->get_fifo_list();
//  std::vector<uint64_t> page_id_fifo;
//  page_id_fifo.reserve(frame_id_fifo.size());
//  for (const frame_id_t it : frame_id_fifo) {
//    page_id_fifo.emplace_back(buffer_frames_[it]->page_id_);
//  }
//  return page_id_fifo;
//}
//
//std::vector<uint64_t> BufferManager::get_lru_list() const {
//  std::vector<frame_id_t> frame_id_lru = replacer_->get_lru_list();
//  std::vector<uint64_t> page_id_lru;
//  page_id_lru.reserve(frame_id_lru.size());
//  for (const frame_id_t  it : frame_id_lru) {
//    page_id_lru.emplace_back(buffer_frames_[it]->page_id_);
//  }
//  return page_id_lru;
//}

}  // namespace moderndbs
