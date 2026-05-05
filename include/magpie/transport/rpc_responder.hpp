#pragma once

#include <functional>
#include <memory>
#include <string>

#include <magpie/schema/base_schema.hpp>
#include <magpie/serializer/value.hpp>
#include <magpie/transport/timeout_error.hpp>

namespace magpie {

/**
 * RpcResponder
 *
 * Abstract base class that handles RPC-style request/response over a transport.
 * Mirrors Python RpcResponder.
 */
class RpcResponder {
public:
    using Object        = Value;
    using RpcHandler    = std::function<Object(const Object&)>;
    using ClientContext = std::shared_ptr<void>;

    explicit RpcResponder(const std::string&          name   = "RpcResponder",
                          std::shared_ptr<BaseSchema> schema = nullptr);
    virtual ~RpcResponder() = default;

    RpcResponder(const RpcResponder&)            = delete;
    RpcResponder& operator=(const RpcResponder&) = delete;

    /**
     * Handles a single incoming request.
     *
     * When a schema is set on this responder, handler is optional — the schema
     * dispatches the request automatically. When no schema is set, handler is required.
     *
     * @param handler    Function(request) -> response. Optional when schema is set.
     * @param timeoutSec Timeout in seconds for waiting for a request.
     *                   < 0 means "no timeout" (transport decides).
     *
     * @return True if a request was handled; False if it timed out.
     *
     * @throws std::runtime_error if already closed or no handler/schema is set.
     * @throws TimeoutError       if transportRecv throws TimeoutError.
     * @throws std::exception     for transport-level or handler errors.
     */
    bool handleOnce(const RpcHandler& handler = nullptr, double timeoutSec = -1.0);

    /**
     * Close the responder and underlying transport.
     * Safe to call multiple times.
     */
    void close();

    bool isClosed() const noexcept { return closed_; }
    const std::string& name() const noexcept { return name_; }
    std::shared_ptr<BaseSchema> schema() const noexcept { return schema_; }

    void send(const Object& response, const ClientContext& clientCtx) {
        transportSend(response, clientCtx);
    }

    void receive(Object& outRequest, ClientContext& outClientCtx, double timeoutSec)  {
        transportRecv(outRequest, outClientCtx, timeoutSec);
    }


protected:
    /**
     * Transport-specific receive.
     *
     * Implementations should:
     *  - block until a request arrives or timeoutSec elapses
     *  - deserialize into Object
     *  - fill clientCtx with opaque context (e.g., ZMQ routing identity)
     *
     * @throws TimeoutError if no request arrives within timeoutSec.
     * @throws std::exception for other transport errors.
     */
    virtual void transportRecv(Object& outRequest,
                               ClientContext& outClientCtx,
                               double timeoutSec) = 0;

    /**
     * Transport-specific send.
     *
     * Implementations should serialize response and send using clientCtx.
     */
    virtual void transportSend(const Object& response,
                               const ClientContext& clientCtx) = 0;

    /**
     * Transport-specific close.
     */
    virtual void transportClose() = 0;

private:
    std::string                 name_;
    bool                        closed_{false};
    std::shared_ptr<BaseSchema> schema_;
};

} // namespace magpie
