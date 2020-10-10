#include "moderndbs/buffer_manager.h"


/*
This is only a dummy implementation of a buffer manager. It does not do any
disk I/O or locking. It also does not respect the page_count and creates a new
buffer for every fixed page and does so without using any latches so it is not
thread-safe.
*/


namespace moderndbs {

char* BufferFrame::get_data() {
    return data.data();
}


BufferManager::BufferManager(size_t page_size, size_t /*page_count*/) : page_size(page_size) {
}


BufferManager::~BufferManager() = default;


BufferFrame& BufferManager::fix_page(uint64_t page_id, bool /*exclusive*/) {
    auto result = pages.emplace(page_id, BufferFrame{page_id});
    auto& page = result.first->second;
    bool is_new = result.second;
    if (is_new) {
        page.data.resize(page_size, 0);
    }
    return page;
}


void BufferManager::unfix_page(BufferFrame& /*page*/, bool /*is_dirty*/) {
}


std::vector<uint64_t> BufferManager::get_fifo_list() const {
    return {};
}


std::vector<uint64_t> BufferManager::get_lru_list() const {
    return {};
}

}  // namespace moderndbs
