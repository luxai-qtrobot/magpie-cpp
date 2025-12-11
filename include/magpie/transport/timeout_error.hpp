#pragma once

#include <stdexcept>
#include <string>

namespace magpie {

class TimeoutError : public std::runtime_error {
public:
    explicit TimeoutError(const std::string& msg)
        : std::runtime_error(msg) {}
};

} // namespace magpie

