#ifndef INCLUDE_MODERNDBS_ERROR_H_
#define INCLUDE_MODERNDBS_ERROR_H_

#include <stdexcept>
#include <string>

namespace moderndbs {

struct NotImplementedException : std::runtime_error {
   public:
   // Error message
   NotImplementedException() : std::runtime_error("not implemented") {
   }
};

} // namespace moderndbs

#endif // INCLUDE_MODERNDBS_ERROR_H_
