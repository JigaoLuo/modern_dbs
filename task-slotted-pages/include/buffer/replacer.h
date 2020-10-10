// Copyright (c) 2019 CMU Database Group
// Use of this source code is governed by a MIT
// license that can be found in the LICENSES/BUSTUB-LICENSE file.

//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// replacer.h
//
// Identification: src/include/buffer/replacer.h
//
// Copyright (c) 2015-2019, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#ifndef INCLUDE_MODERNDBS_REPLACER_H
#define INCLUDE_MODERNDBS_REPLACER_H

#pragma once

#include "common/config.h"

namespace moderndbs {

/**
 * Replacer is an abstract class that tracks page usage.
 */
class Replacer {
public:
    Replacer() = default;
    virtual ~Replacer() = default;

    /**
     * Remove the victim frame as defined by the replacement policy.
     * @param[out] frame_id id of frame that was removed, nullptr if no victim was found
     * @return true if a victim frame was found, false otherwise
     */
    virtual bool Victim(frame_id_t *frame_id) = 0;

    /**
     * Pins a frame, indicating that it should not be victimized until it is unpinned.
     * @param frame_id the id of the frame to pin
     */
    virtual void Pin(frame_id_t frame_id) = 0;

    /**
     * Unpins a frame, indicating that it can now be victimized.
     * @param frame_id the id of the frame to unpin
     */
    virtual void Unpin(frame_id_t frame_id) = 0;

    /** @return the number of elements in the replacer that can be victimized */
    virtual size_t Size() const = 0;
};

}  // namespace moderndbs


#endif //INCLUDE_MODERNDBS_REPLACER_H
