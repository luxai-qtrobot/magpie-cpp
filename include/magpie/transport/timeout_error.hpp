#pragma once

#include <stdexcept>
#include <string>

namespace magpie {

class TimeoutError : public std::runtime_error {
public:
    explicit TimeoutError(const std::string& msg)
        : std::runtime_error(msg) {}
};


// For RPC acknowledgments
class AckTimeoutError : public TimeoutError {
public:
    explicit AckTimeoutError(const std::string& msg)
        : TimeoutError(msg) {}
};

// For RPC reply timeouts
class ReplyTimeoutError : public TimeoutError {
public:
    explicit ReplyTimeoutError(const std::string& msg)
        : TimeoutError(msg) {}
};

} // namespace magpie

