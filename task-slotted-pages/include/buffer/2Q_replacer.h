#ifndef INCLUDE_MODERNDBS_2Q_REPLACER_H
#define INCLUDE_MODERNDBS_2Q_REPLACER_H

#pragma once

#include <list>
#include <vector>
#include <unordered_map>
#include <shared_mutex>  // NOLINT

#include "buffer/replacer.h"
#include "common/config.h"

namespace moderndbs {

/**
 * 2QReplacer implements the Two Queue replacement policy, which approximates the Least Recently Used policy.
 */
class TwoQReplacer : public Replacer {
public:
  /**
   * Create a new 2QReplacer.
   * @param num_pages the maximum number of pages the 2QReplacer will be required to store
   */
  explicit TwoQReplacer(size_t num_pages);

  /**
   * Destroys the 2QReplacer.
   */
  ~TwoQReplacer() override;

  /**
   * If 2Q is non-empty, pop the head member from 2Q to argument "frame_id", and
   * return true. If 2Q is empty, return false
   */
  bool Victim(frame_id_t *frame_id) override;

  /**
   * This method should be called after a page is pinned to a frame in the Buffer Pool Manager.
   * It should remove the frame containing the pinned page from the 2Q Replacer.
   * @param frame_id  the page is pinned to a frame in the Buffer Pool Manager.
   */
  void Pin(frame_id_t frame_id) override;

  /**
   * This method should be called when the pin_count of a page becomes 0.
   * This method should add the frame containing the unpinned page to the 2Q Replacer.
   * @param frame_id  the page having pin_count becoming 0
   */
  void Unpin(frame_id_t frame_id) override;

  /** @return number of COLD / unpinned / evict-able frames in fifo */
  size_t FifoSize() const;

  /** @return number of HOT / unpinned / evict-able frames in lru */
  size_t LruSize() const;

  /** @return current number of unpinned / evict-able frames of 2Q */
  size_t Size() const override;

  /**
   * Returns the page ids of all framd id that are in the FIFO list in FIFO order.
   * Is not thread-safe.
   */
  std::vector<frame_id_t> get_fifo_list() const;

  /**
   * Returns the page ids of all framd id that are in the LRU list in LRU order.
   * Is not thread-safe.
   */
  std::vector<frame_id_t> get_lru_list() const;


private:
  /** hot page marker: true for hot page, in lru list, otherwise cold page in fifo list */
  enum page_hotness {
    COLD,         // pin_count == 0, in fifo list, can be evicted
    HOT,          // pin_count == 0, in lru list, can be evicted
    COLD_PINNED,  // pin_count != 0, in fifo list as PLACEHOLER, can't be evicted
    HOT_PINNED    // pin_count != 0, in fifo list as PLACEHOLER, can't be evicted
  };
  /** maximal page capacity of replacer */
  const size_t num_pages;
  /** number of COLD in fifo */
  size_t fifo_size{0};
  /** number of HOT in lru */
  size_t lru_size{0};
  /** cold pages in fifo list */
  std::list<frame_id_t> fifo_list;
  /** hot pages in lru list */
  std::list<frame_id_t> lru_list;
  /** hash table for O(1) access */
  std::unordered_map<frame_id_t, std::pair<std::list<frame_id_t>::iterator, page_hotness>> ht;
  /** replacer latch */
  mutable std::shared_mutex latch;
};

}  // namespace moderndbs


#endif //INCLUDE_MODERNDBS_2Q_REPLACER_H
