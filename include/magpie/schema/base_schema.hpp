#pragma once

#include <magpie/serializer/value.hpp>

namespace magpie {

class BaseSchema {
public:
    virtual ~BaseSchema() = default;

    /**
     * Dispatch a deserialized request and return a response Value.
     * Returns Value::null() when no reply should be sent (notifications).
     */
    virtual Value dispatch(const Value& request) = 0;

    /**
     * Build a request envelope for the given method and params.
     * Used by RpcRequester::call(method, params, timeout).
     */
    virtual Value wrap(const std::string& method, const Value::Dict& params = {}) const = 0;

    /**
     * Extract the result from a response envelope.
     * Throws on protocol-level errors.
     */
    virtual Value unwrap(const Value& response) const = 0;
};

} // namespace magpie
