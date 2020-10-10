#include "buffer/2Q_replacer.h"

namespace moderndbs {

TwoQReplacer::TwoQReplacer(size_t num_pages) : num_pages(num_pages) {}

TwoQReplacer::~TwoQReplacer() = default;

bool TwoQReplacer::Victim(frame_id_t *frame_id) {
  std::unique_lock u_lock(latch);
  // 0. Check the size, if any can be victimized
  assert(ht.size() == fifo_list.size() + lru_list.size());
  assert(ht.size() > 0);
  assert(ht.size() <= num_pages);
  if (fifo_size + lru_size == 0) return false;
  // 1. Find from end without PINNED of the 2Q: First try fifo list. then lru list
  // 2. Erase from the hash table
  std::list<frame_id_t>* erase_list;
  page_hotness comp;
  if (fifo_size == 0) {
    erase_list = &lru_list;
    lru_size--;
    comp = HOT;
  } else {
    erase_list = &fifo_list;
    fifo_size--;
    comp = COLD;
  }
  for (auto it = erase_list->rbegin(); it != erase_list->rend(); ++it) {
    frame_id_t frame_id_candidate = *it;
    const auto& got = ht.find(frame_id_candidate);
    auto page_hotness = got->second.second;
    if (page_hotness == comp) {
      *frame_id = frame_id_candidate;
      erase_list->erase(std::next(it).base());
      ht.erase(got);
      assert(ht.size() == fifo_list.size() + lru_list.size());
      return true;
    }
  }
  assert(false);  // Should not be here, always finds a victim
  return false;
}

void TwoQReplacer::Pin(frame_id_t frame_id) {
  std::unique_lock u_lock(latch);
  assert(ht.size() == fifo_list.size() + lru_list.size());
  assert(ht.size() <= num_pages);
  assert(static_cast<unsigned long>(frame_id) <= num_pages);
  // 1. Check if the frame_id already there
  const auto& got = ht.find(frame_id);
  if (got != ht.end()) {
    // 2. value already there, change it to pinned (just as a placer holder for unpin function deciding to go to lru or fifo)
    auto& page_hotness = got->second.second;
    if (page_hotness == COLD) {
      page_hotness = COLD_PINNED;
      fifo_size--;
    } else if (page_hotness == HOT) {
      page_hotness = HOT_PINNED;
      lru_size--;
    }
  }
  assert(ht.size() == fifo_list.size() + lru_list.size());
}

void TwoQReplacer::Unpin(frame_id_t frame_id) {
  std::unique_lock u_lock(latch);
  assert(ht.size() == fifo_list.size() + lru_list.size());
  assert(ht.size() <= num_pages);
  assert(static_cast<unsigned long>(frame_id) <= num_pages);
  // 1. Check if the frame_id already there
  const auto& got = ht.find(frame_id);
  if (got == ht.end()) {
    // 2.1. value not there
    assert(ht.size() < num_pages);
    // 2.1.2 insert frame_id into FIFO as a cold page
    fifo_list.emplace_front(frame_id);
    ht.emplace(std::piecewise_construct,
               std::forward_as_tuple(frame_id),
               std::forward_as_tuple(fifo_list.begin(), COLD));
    fifo_size++;
  } else {
    // 2.2 value already there, adjust the 2Q depending on hot or cold page
    //   hot page, adjust lru list
    //   cold page becoming hot page, remove from fifo list, then add to lur list
    auto& page_hotness = got->second.second;
    lru_list.splice(lru_list.begin(),
                    ((page_hotness == HOT || page_hotness == HOT_PINNED) ? lru_list : fifo_list),
                    got->second.first);
    if (page_hotness == COLD) {
      fifo_size--;
      lru_size++;
      page_hotness = HOT;
    } else if (page_hotness == HOT_PINNED || page_hotness == COLD_PINNED ) {
      lru_size++;
      page_hotness = HOT;
    }
  }
  assert(ht.size() == fifo_list.size() + lru_list.size());
}

size_t TwoQReplacer::Size() const {
  std::shared_lock<std::shared_mutex> lock(latch);
  assert(ht.size() == fifo_list.size() + lru_list.size());
  assert(ht.size() <= num_pages);
  return fifo_size + lru_size;
}

size_t TwoQReplacer::FifoSize() const {
  std::shared_lock<std::shared_mutex> lock(latch);
  assert(ht.size() == fifo_list.size() + lru_list.size());
  assert(ht.size() <= num_pages);
  return fifo_size;
}

size_t TwoQReplacer::LruSize() const {
  std::shared_lock<std::shared_mutex> lock(latch);
  assert(ht.size() == fifo_list.size() + lru_list.size());
  assert(ht.size() <= num_pages);
  return lru_size;
}

std::vector<frame_id_t> TwoQReplacer::get_fifo_list() const {
  std::vector<frame_id_t> frame_id_fifo;
  for (auto it = fifo_list.crbegin(); it != fifo_list.crend(); it++) {
    frame_id_fifo.emplace_back(*it);
  }
  return frame_id_fifo;
}

std::vector<frame_id_t> TwoQReplacer::get_lru_list() const {
  std::vector<frame_id_t> frame_id_lru;
  for (auto it = lru_list.crbegin(); it != lru_list.crend(); it++) {
    frame_id_lru.emplace_back(*it);
  }
  return frame_id_lru;
}

}  // namespace moderndbs
