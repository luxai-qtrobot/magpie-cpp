#pragma once

#include <magpie/transport/webrtc_signaler.hpp>

#include <memory>
#include <mutex>
#include <string>

namespace magpie {

class MqttConnection;

#ifdef MAGPIE_WITH_MQTT
/** MQTT signaling adapter. The caller owns and connects the MQTT connection. */
class MqttSignaler final : public WebRtcSignaler {
public:
    MqttSignaler(std::shared_ptr<MqttConnection> connection, std::string sessionId);
    ~MqttSignaler() override;

    const std::string& sessionId() const noexcept override { return sessionId_; }
    void publish(const std::uint8_t* data, std::size_t size) override;
    void subscribe(MessageCallback callback) override;
    void unsubscribe() override;
    void disconnect() override;

private:
    std::shared_ptr<MqttConnection> connection_;
    std::string sessionId_;
    std::string topic_;
    std::mutex mutex_;
    std::uint64_t handle_{0};
};
#endif

} // namespace magpie
