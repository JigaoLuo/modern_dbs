#ifndef INCLUDE_MODERNDBS_ERROR_H_
#define INCLUDE_MODERNDBS_ERROR_H_

#include <stdexcept>
#include <string>

namespace moderndbs {

struct SchemaParseError: std::exception {
    // Constructor
    explicit SchemaParseError(const char *what): message_(what) {}
    // Constructor
    explicit SchemaParseError(std::string what): message_(std::move(what)) {}
    // Destructor
    ~SchemaParseError() noexcept override = default;
    // Get error message
    const char *what() const noexcept override { return message_.c_str(); }

 protected:
    // Error message
    std::string message_;
};

}  // namespace moderndbs

#endif  // INCLUDE_MODERNDBS_ERROR_H_
