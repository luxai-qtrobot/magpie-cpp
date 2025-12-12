#pragma once

#include <memory>
#include <string>

#include <magpie/serializer/value.hpp>
#include <magpie/serializer/serializer.hpp>
#include <magpie/transport/rpc_requester.hpp>

struct zmq_ctx_t;

namespace magpie {

/**
 * ZmqRpcRequester
 *
 * C++ equivalent of Python ZMQRpcRequester using a ZeroMQ DEALER socket.
 * It sends a request object and expects:
 *
 *   Ack:   { "rid": <string>, "ack": true }
 *   Reply: { "rid": <string>, "payload": <Value> }
 *
 * Serialization is done via the shared Serializer<Value>, typically
 * MsgpackSerializer by default.
 */
class ZmqRpcRequester : public RpcRequester {
public:
    using Object = Value;

    ZmqRpcRequester(const std::string& endpoint,
                    std::shared_ptr<Serializer> serializer = nullptr,                    
                    const std::string& identity = std::string(),
                    double ackTimeoutSec = 2.0);

    ~ZmqRpcRequester() override;

    ZmqRpcRequester(const ZmqRpcRequester&)            = delete;
    ZmqRpcRequester& operator=(const ZmqRpcRequester&) = delete;

protected:
    Object transportCall(const Object& request, double timeoutSec) override;
    void   transportClose() override;

private:
    bool socketReceive(Object& outObj, double timeoutSec);

    static bool       startsWith(const std::string& s, const std::string& prefix);
    static zmq_ctx_t* sharedInprocContext();

    std::string                 endpoint_;
    double                      ackTimeoutSec_;
    std::shared_ptr<Serializer> serializer_;

    zmq_ctx_t* context_{nullptr};
    void*      socket_{nullptr};
    bool       ownsContext_{false};
};

} // namespace magpie
