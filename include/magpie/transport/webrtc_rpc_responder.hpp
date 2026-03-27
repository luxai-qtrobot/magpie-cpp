#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

#include <magpie/serializer/value.hpp>
#include <magpie/transport/webrtc_connection.hpp>
#include <magpie/transport/rpc_responder.hpp>

namespace magpie {

/**
 * WebRtcRpcResponder
 *
 * Implements the server side of RPC over the WebRTC data channel.
 * Wire-protocol compatible with Python WebRTCRpcResponder.
 *
 * Receives requests, sends an immediate ACK, invokes the user-supplied
 * handler, and sends the reply — all on the same bidirectional data channel.
 *
 * Protocol:
 *   Receives on data channel:
 *     { "type":"rpc_req", "service":"<service>", "rid":"<ulid>", "payload":<request> }
 *
 *   Sends ACK:
 *     { "type":"rpc_ack", "rid":"<ulid>" }
 *
 *   Sends reply:
 *     { "type":"rpc_rep", "rid":"<ulid>", "payload":<response> }
 *
 * @code
 * auto conn = std::make_shared<WebRtcConnection>(signalConn, "my-robot");
 * conn->connect(30.0);
 *
 * WebRtcRpcResponder rsp(conn, "robot/motion");
 * rsp.handleOnce([](const Value& req) { return Value::fromString("ok"); }, 10.0);
 *
 * rsp.close();
 * conn->disconnect();
 * @endcode
 */
class WebRtcRpcResponder : public RpcResponder {
public:
    using Object = Value;

    /**
     * @param connection   Shared, already-connected WebRtcConnection.
     * @param serviceName  Service identifier — must match the requester.
     * @param instanceName Optional human-readable name for logging.
     */
    explicit WebRtcRpcResponder(std::shared_ptr<WebRtcConnection> connection,
                                 const std::string&                serviceName,
                                 const std::string&                instanceName = std::string());

    ~WebRtcRpcResponder() override;

    WebRtcRpcResponder(const WebRtcRpcResponder&)            = delete;
    WebRtcRpcResponder& operator=(const WebRtcRpcResponder&) = delete;

protected:
    void transportRecv(Object& outRequest, ClientContext& outClientCtx, double timeoutSec) override;
    void transportSend(const Object& response, const ClientContext& clientCtx)             override;
    void transportClose()                                                                   override;

private:
    // Client context stored per request: holds the rid for the reply
    struct ClientCtxData {
        std::string rid;
    };

    void onRequestMessage(const Value& msg);

    std::shared_ptr<WebRtcConnection>  connection_;
    std::string                        serviceName_;
    WebRtcConnection::CallbackHandle   reqHandle_{0};

    std::mutex              reqMutex_;
    std::condition_variable reqCv_;
    std::deque<Value>       reqQueue_;   // full rpc_req message dicts
    std::atomic<bool>       reqClosed_{false};
};

} // namespace magpie
