#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <magpie/serializer/serializer.hpp>
#include <magpie/serializer/value.hpp>
#include <magpie/transport/rpc_responder.hpp>

struct zmq_ctx_t;

namespace magpie {

/**
 * ZmqRpcResponder
 *
 * C++ equivalent of Python ZMQRpcResponder using a ZeroMQ ROUTER socket.
 *
 * Receives:
 *   Request: { "rid": <string>, "payload": <Value> }
 * Sends:
 *   Ack:     { "rid": <string>, "ack": true }
 *   Reply:   { "rid": <string>, "payload": <Value> }
 */
class ZmqRpcResponder : public RpcResponder {
public:
    using Object = Value;

    ZmqRpcResponder(const std::string&          endpoint,
                    std::shared_ptr<Serializer> serializer = nullptr,
                    bool                        bind       = true,
                    std::shared_ptr<BaseSchema> schema     = nullptr);

    ~ZmqRpcResponder() override;

    ZmqRpcResponder(const ZmqRpcResponder&)            = delete;
    ZmqRpcResponder& operator=(const ZmqRpcResponder&) = delete;

protected:
    void transportRecv(Object& outRequest,
                       ClientContext& outClientCtx,
                       double timeoutSec) override;

    void transportSend(const Object& response,
                       const ClientContext& clientCtx) override;

    void transportClose() override;

private:
    struct ClientCtxData {
        std::vector<std::uint8_t> identity; // ROUTER identity frame
        std::string               rid;      // request id
    };

    // Poll+recv multipart, return false if socket closed; throws TimeoutError on timeout.
    bool socketReceive(Object& outObj,
                       std::vector<std::uint8_t>& outIdentity,
                       double timeoutSec);

    static bool       startsWith(const std::string& s, const std::string& prefix);
    static zmq_ctx_t* sharedInprocContext();

    std::string                 endpoint_;
    bool                        bind_{true};
    std::shared_ptr<Serializer> serializer_;

    zmq_ctx_t* context_{nullptr};
    void*      socket_{nullptr};
    bool       ownsContext_{false};
};

} // namespace magpie
