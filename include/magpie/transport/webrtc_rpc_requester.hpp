#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <magpie/serializer/value.hpp>
#include <magpie/transport/webrtc_connection.hpp>
#include <magpie/transport/rpc_requester.hpp>

namespace magpie {

/**
 * WebRtcRpcRequester
 *
 * Implements RPC-style request/response over the WebRTC data channel using the
 * same wire protocol as Python WebRTCRpcRequester and JS WebRtcRpcRequester.
 *
 * Because the data channel is a bidirectional P2P pipe, no reply_to topic is
 * needed — both request and reply travel on the same channel, demuxed by rid.
 *
 * Protocol:
 *   Request  → data channel:
 *     { "type":"rpc_req", "service":"<service>", "rid":"<ulid>", "payload":<request> }
 *
 *   ACK      ← data channel:
 *     { "type":"rpc_ack", "rid":"<ulid>" }
 *
 *   Reply    ← data channel:
 *     { "type":"rpc_rep", "rid":"<ulid>", "payload":<response> }
 *
 * @code
 * auto conn = std::make_shared<WebRtcConnection>(signalConn, "my-robot");
 * conn->connect(30.0);
 *
 * WebRtcRpcRequester req(conn, "robot/motion");
 * Value result = req.call(Value::fromString("move"), 5.0);
 *
 * req.close();
 * conn->disconnect();
 * @endcode
 */
class WebRtcRpcRequester : public RpcRequester {
public:
    using Object = Value;

    /**
     * @param connection    Shared, already-connected WebRtcConnection.
     * @param serviceName   Service identifier — must match the responder.
     * @param instanceName  Optional human-readable name for logging.
     * @param ackTimeoutSec Timeout for the initial ACK (default: 2.0s).
     */
    explicit WebRtcRpcRequester(std::shared_ptr<WebRtcConnection> connection,
                                 const std::string&                serviceName,
                                 const std::string&                instanceName  = std::string(),
                                 double                            ackTimeoutSec = 2.0,
                                 std::shared_ptr<BaseSchema>       schema        = nullptr);

    ~WebRtcRpcRequester() override;

    WebRtcRpcRequester(const WebRtcRpcRequester&)            = delete;
    WebRtcRpcRequester& operator=(const WebRtcRpcRequester&) = delete;

protected:
    Object transportCall(const Object& request, double timeoutSec) override;
    void   transportClose() override;

private:
    void onReplyMessage(const Value& msg);

    struct PendingCall {
        std::mutex              mtx;
        std::condition_variable cvAck;
        std::condition_variable cvReply;
        bool        acked{false};
        bool        replied{false};
        Object      replyPayload;
        std::string errorMsg;
    };

    void failAllPending(const std::string& err);

    std::shared_ptr<WebRtcConnection> connection_;
    std::string                       serviceName_;
    double                            ackTimeoutSec_;

    std::mutex                                          pendingMtx_;
    std::unordered_map<std::string,
                       std::shared_ptr<PendingCall>>    pending_;

    std::atomic<bool> closing_{false};
};

} // namespace magpie
