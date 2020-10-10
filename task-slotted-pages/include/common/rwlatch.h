#ifndef INCLUDE_MODERNDBS_RWLATCH_H
#define INCLUDE_MODERNDBS_RWLATCH_H

#pragma once

#include <climits>
#include <condition_variable>  // NOLINT
#include <mutex>               // NOLINT
#include <shared_mutex>        // NOLINT

#include "common/config.h"

namespace moderndbs {

/**
 * Reader-Writer shared_mutex backed by std::mutex.
 * Thread safe
 */
class ReaderWriterLatch {

public:
    ReaderWriterLatch() = default;
    ~ReaderWriterLatch() = default;

    DISALLOW_COPY(ReaderWriterLatch);

    /**
     * Acquire a write latch.
     */
    void WLock() {
        shared_mutex_.lock();
        writer_entered_ = true;
    }

    /**
     * Release a write latch.
     */
    void WUnlock() {
        writer_entered_ = false;
        shared_mutex_.unlock();
    }

    /**
     * Acquire a read latch.
     */
    void RLock() {
        shared_mutex_.lock_shared();
    }

    /**
     * Release a read latch.
     */
    void RUnlock() {
        shared_mutex_.unlock_shared();
    }

    std::shared_mutex shared_mutex_;
private:
    bool writer_entered_{false};
};

}  // namespace moderndbs

#endif //INCLUDE_MODERNDBS_RWLATCH_H
