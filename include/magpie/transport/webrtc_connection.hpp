#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <magpie/serializer/value.hpp>
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/webrtc_options.hpp>

namespace magpie {

/**
 * WebRtcConnection
 *
 * Manages a WebRTC peer connection using MQTT as the signaling transport.
 * Multiple WebRtcPublisher, WebRtcSubscriber, WebRtcRpcRequester, and
 * WebRtcRpcResponder instances can share one WebRtcConnection — mirroring
 * the MqttConnection pattern.
 *
 * Signaling runs over a dedicated MQTT topic:
 *   magpie/webrtc/<sessionId>/signal
 *
 * Role (offerer vs answerer) is auto-negotiated: both peers broadcast "hello"
 * messages; the peer with the lexicographically higher peerId creates the offer.
 *
 * Data channel wire format (msgpack-encoded dicts, identical to Python/JS):
 *   pub:     { "type":"pub",     "topic":"...", "payload":<value> }
 *   rpc_req: { "type":"rpc_req", "service":"...", "rid":"...", "payload":<value> }
 *   rpc_ack: { "type":"rpc_ack", "rid":"..." }
 *   rpc_rep: { "type":"rpc_rep", "rid":"...", "payload":<value> }
 *
 * @code
 * auto sig = std::make_shared<MqttConnection>("mqtt://broker.hivemq.com:1883");
 * sig->connect();
 *
 * auto conn = std::make_shared<WebRtcConnection>(sig, "my-robot");
 * if (!conn->connect(30.0)) {
 *     std::cerr << "peer not found\n";
 * }
 * // ... use with WebRtcPublisher / WebRtcSubscriber ...
 * conn->disconnect();
 * @endcode
 */
class WebRtcConnection {
public:
    using CallbackHandle = uint64_t;

    /// Callback for incoming pub messages: (payload_value, topic)
    using DataCallback =
        std::function<void(const Value& payload, const std::string& topic)>;

    /// Callback for incoming RPC requests: (full_rpc_req_message_dict)
    using RpcRequestCallback =
        std::function<void(const Value& msg)>;

    /// One-shot callback for RPC ack/reply: (full_rpc_ack_or_rep_message_dict)
    using RpcReplyCallback =
        std::function<void(const Value& msg)>;

    /// Callback for incoming video frames from the magpie-media channel.
    /// Receives the raw frame-dict Value (same format as ImageFrameRaw::toDict()).
    using VideoCallback =
        std::function<void(const Value& frameDict)>;

    /// Callback for incoming audio frames from the magpie-media channel.
    /// Receives the raw frame-dict Value (same format as AudioFrameRaw::toDict()).
    using AudioCallback =
        std::function<void(const Value& frameDict)>;

    /**
     * Construct a WebRtcConnection.
     *
     * @param signalConn  Shared, already-connected MqttConnection used only for signaling.
     * @param sessionId   Session identifier — must match the remote peer exactly.
     * @param options     WebRTC configuration (ICE servers, reconnect, data channel settings).
     */
    explicit WebRtcConnection(std::shared_ptr<MqttConnection> signalConn,
                               const std::string&              sessionId,
                               WebRtcOptions                   options = WebRtcOptions{});

    ~WebRtcConnection();

    WebRtcConnection(const WebRtcConnection&)            = delete;
    WebRtcConnection& operator=(const WebRtcConnection&) = delete;

    /**
     * Start the hello loop and block until the data channel is open or timeout.
     *
     * @param timeoutSec  Maximum seconds to wait for the peer (default: 30).
     * @return true if connected, false on timeout.
     */
    bool connect(double timeoutSec = 30.0);

    /**
     * Close the peer connection, remove signaling subscription, and release
     * all resources.  Safe to call multiple times.
     */
    void disconnect();

    /** @return true if the data channel is open and messages can be sent. */
    bool isConnected() const;

    /** @return the local peer ID used during the current (or last) session. */
    const std::string& peerId() const;

    /** @return the session ID shared with the remote peer. */
    const std::string& sessionId() const;

    /**
     * @return true if the "magpie-media" unreliable channel is used for
     *         video/audio (the default), false if the reliable "magpie"
     *         channel is used instead.
     */
    bool useMediaChannels() const;

    // ---- Registration API (used by pub/sub and RPC transport classes) ----

    /**
     * Register a callback for incoming pub messages on a topic.
     * Returns a handle for later removal.
     */
    CallbackHandle addPubCallback(const std::string& topic, DataCallback callback);
    void           removePubCallback(const std::string& topic, CallbackHandle handle);

    /**
     * Register a callback for incoming RPC requests addressed to a service.
     */
    CallbackHandle addRpcRequestCallback(const std::string& service,
                                          RpcRequestCallback callback);
    void           removeRpcRequestCallback(const std::string& service, CallbackHandle handle);

    /**
     * Register a one-shot callback for the ACK/reply of an in-flight RPC call.
     * Automatically removed once the reply is received.
     */
    void addRpcReplyCallback(const std::string& rid, RpcReplyCallback callback);
    void removeRpcReplyCallback(const std::string& rid);

    /**
     * Serialize msg as msgpack and send it on the data channel.
     * Silently drops the message if the channel is not open.
     */
    void sendData(const Value& msg);

    /**
     * Register a callback for incoming video frames on the magpie-media channel.
     * Returns a handle for later removal.
     */
    CallbackHandle addVideoCallback(VideoCallback callback);
    void           removeVideoCallback(CallbackHandle handle);

    /**
     * Register a callback for incoming audio frames on the magpie-media channel.
     * Returns a handle for later removal.
     */
    CallbackHandle addAudioCallback(AudioCallback callback);
    void           removeAudioCallback(CallbackHandle handle);

    /**
     * Serialize msg as msgpack and send it on the magpie-media unreliable channel.
     * Only use when useMediaChannels() is true.
     * msg should be: {"kind":"video"|"audio", "topic":"...", "payload":<frame-dict>}
     * Silently drops the message if the channel is not open.
     */
    void sendMediaFrame(const Value& msg);

private:
    // PIMPL — keeps libdatachannel headers out of this public header.
    // shared_ptr (not unique_ptr) so that lambdas inside Impl can hold
    // weak_ptr<Impl> safely across the connection lifetime.
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace magpie
