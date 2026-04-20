#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <magpie/transport/mqtt_options.hpp>

namespace magpie {

/**
 * MqttConnection
 *
 * Manages a single shared MQTT broker connection using the Paho C++ library.
 * Multiple MqttStreamWriter, MqttStreamReader, MqttRpcRequester, and MqttRpcResponder
 * instances can share one MqttConnection to reuse a single TCP/TLS connection.
 *
 * URI schemes:
 *   mqtt://host:port     Plain TCP (default port 1883)
 *   mqtts://host:port    TLS/TCP   (default port 8883)
 *   ws://host:port/path  WebSocket (default port 9001)
 *   wss://host:port/path TLS WebSocket (default port 8884)
 *
 * @code
 * auto conn = std::make_shared<MqttConnection>("mqtt://broker.hivemq.com:1883");
 * conn->connect();
 * // ... use with MqttStreamWriter / MqttStreamReader ...
 * conn->disconnect();
 * @endcode
 */
class MqttConnection {
public:
    /// Opaque handle returned by addSubscription(); used to remove it later.
    using SubscriptionHandle = uint64_t;

    /// Callback signature: (topic, raw_payload_bytes, payload_size)
    using MessageCallback = std::function<void(const std::string& topic,
                                               const uint8_t*     data,
                                               std::size_t        size)>;

    /**
     * Construct a connection (does not connect yet; call connect()).
     *
     * @param uri       Broker URI, e.g. "mqtt://localhost:1883"
     * @param clientId  MQTT client ID; auto-generated if empty
     * @param options   Extended options (TLS, auth, reconnect, etc.)
     */
    explicit MqttConnection(const std::string& uri,
                             const std::string& clientId = std::string(),
                             MqttOptions        options  = MqttOptions{});

    ~MqttConnection();

    MqttConnection(const MqttConnection&)            = delete;
    MqttConnection& operator=(const MqttConnection&) = delete;

    /**
     * Establish connection to the broker.
     *
     * @param timeoutSec  How long to wait for the CONNACK, in seconds.
     * @throws std::runtime_error on failure or timeout.
     */
    void connect(double timeoutSec = 10.0);

    /**
     * Gracefully disconnect from the broker.
     */
    void disconnect();

    /**
     * Publish raw bytes to a topic.
     *
     * @param topic   MQTT topic string.
     * @param data    Pointer to payload bytes.
     * @param size    Payload size in bytes.
     * @param qos     QoS level (0, 1, or 2). Uses options.defaults.publishQos if -1.
     * @param retain  Retained message flag. Uses options.defaults.publishRetain if not set.
     */
    void publish(const std::string& topic,
                 const uint8_t*     data,
                 std::size_t        size,
                 int                qos    = -1,
                 bool               retain = false);

    /**
     * Register a callback for messages matching topicFilter.
     * MQTT wildcards are supported: '+' (single level) and '#' (multi-level).
     *
     * @param topicFilter  Topic or pattern to subscribe to.
     * @param callback     Called from the paho network thread; keep it fast.
     * @param qos          Subscribe QoS. Uses options.defaults.subscribeQos if -1.
     * @return             Handle to pass to removeSubscription().
     */
    SubscriptionHandle addSubscription(const std::string& topicFilter,
                                       MessageCallback    callback,
                                       int                qos = -1);

    /**
     * Unregister a previously added callback.
     * When all callbacks for a topicFilter are removed, the client unsubscribes.
     */
    void removeSubscription(const std::string& topicFilter,
                             SubscriptionHandle handle);

    /** @return true if currently connected to the broker. */
    bool isConnected() const;

    /** @return the resolved default publish QoS from options. */
    int defaultPublishQos()    const noexcept;

    /** @return the resolved default subscribe QoS from options. */
    int defaultSubscribeQos()  const noexcept;

    /** @return the resolved default publish retain flag from options. */
    bool defaultPublishRetain() const noexcept;

private:
    // PIMPL – keeps paho headers out of this public header.
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace magpie
