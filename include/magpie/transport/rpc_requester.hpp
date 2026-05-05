#pragma once

#include <memory>
#include <string>

#include <magpie/schema/base_schema.hpp>
#include <magpie/serializer/value.hpp>
#include <magpie/transport/timeout_error.hpp>

namespace magpie {

/**
 * RpcRequester
 *
 * Abstract base class for performing RPC-style request/response operations
 * over an abstract transport. Mirrors Python RpcRequester.
 */
class RpcRequester {
public:
    using Object = Value;

    explicit RpcRequester(const std::string&          name   = "RpcRequester",
                          std::shared_ptr<BaseSchema> schema = nullptr);
    virtual ~RpcRequester() = default;

    RpcRequester(const RpcRequester&)            = delete;
    RpcRequester& operator=(const RpcRequester&) = delete;

    /**
     * Performs a raw RPC call (no schema).
     *
     * @param request    Request payload to send.
     * @param timeoutSec Timeout in seconds. < 0 means no timeout.
     * @throws std::runtime_error if already closed.
     * @throws ReplyTimeoutError / AckTimeoutError / TimeoutError on timeout.
     */
    Object call(const Object& request, double timeoutSec = -1.0);

    /**
     * Schema-based RPC call (requires schema set at construction).
     * Wraps the call in a JSON-RPC 2.0 envelope and unwraps the response.
     *
     * @param method     JSON-RPC method name.
     * @param params     Named parameters as Value::Dict.
     * @param timeoutSec Timeout in seconds. < 0 means no timeout.
     * @throws std::runtime_error if already closed or no schema set.
     * @throws JsonRpcError       if the server returns a JSON-RPC error.
     * @throws ReplyTimeoutError / AckTimeoutError / TimeoutError on timeout.
     */
    Object call(const std::string& method,
                const Value::Dict& params      = {},
                double             timeoutSec  = -1.0);

    /**
     * Close the requester and underlying transport.
     * Safe to call multiple times.
     */
    void close();

    bool isClosed() const noexcept { return closed_; }
    
    const std::string& name() const noexcept { return name_; }

protected:
    /**
     * Transport-specific implementation of a single RPC call.
     * Implementations should:
     *  - serialize the request if needed
     *  - send it via the transport
     *  - wait for and receive the reply
     *  - deserialize it into an Object
     *
     * May throw AckTimeoutError, ReplyTimeoutError, TimeoutError, etc.
     */
    virtual Object transportCall(const Object& request, double timeoutSec) = 0;

    /**
     * Transport-specific close.
     * Called from close().
     */
    virtual void transportClose() = 0;

private:
    std::string                 name_;
    bool                        closed_{false};
    std::shared_ptr<BaseSchema> schema_;
};

} // namespace magpie
