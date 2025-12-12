#pragma once

#include <memory>
#include <string>

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

    explicit RpcRequester(const std::string& name = "RpcRequester");
    virtual ~RpcRequester() = default;

    RpcRequester(const RpcRequester&)            = delete;
    RpcRequester& operator=(const RpcRequester&) = delete;

    /**
     * Performs an RPC call using the underlying transport.
     *
     * @param request    Request payload to send.
     * @param timeoutSec Timeout in seconds for waiting for a reply.
     *                   < 0 means "no timeout" (transport decides).
     *
     * @return Response object.
     *
     * @throws std::runtime_error if already closed.
     * @throws ReplyTimeoutError  if no reply arrives in time.
     * @throws AckTimeoutError    if no acknowledgment arrives in time.
     * @throws TimeoutError       for other timeout conditions.
     * @throws std::exception     for transport-level errors.
     */
    Object call(const Object& request, double timeoutSec = -1.0);

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
    std::string name_;
    bool        closed_{false};
};

} // namespace magpie
