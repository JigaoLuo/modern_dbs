#ifndef INCLUDE_MODERNDBS_SEGMENT_H_
#define INCLUDE_MODERNDBS_SEGMENT_H_

#include <array>
#include <atomic>
#include "moderndbs/buffer_manager.h"

namespace moderndbs {

class Segment {
    public:
    /// Constructor.
    /// @param[in] segment_id       Id of the segment.
    /// @param[in] buffer_manager   The buffer manager that should be used by the segment.
    Segment(uint16_t segment_id, BufferManager& buffer_manager) : segment_id(segment_id), buffer_manager(buffer_manager) {}

    /// The segment id
    uint16_t segment_id;
    protected:
    /// The buffer manager
    BufferManager& buffer_manager;
};

}  // namespace moderndbs

#endif // INCLUDE_MODERNDBS_SEGMENT_H_
