#include <magpie/transport/webrtc_mqtt_signaler.hpp>
#include <magpie/transport/webrtc_connection.hpp>
#include <magpie/transport/mqtt_connection.hpp>

#include <stdexcept>
#include <utility>

namespace magpie {

MqttSignaler::MqttSignaler(std::shared_ptr<MqttConnection> connection, std::string sessionId)
    : connection_(std::move(connection)), sessionId_(std::move(sessionId)),
      topic_("magpie/webrtc/" + sessionId_ + "/signal") {
    if (!connection_ || sessionId_.empty())
        throw std::invalid_argument("MqttSignaler: connection and session ID are required");
}

MqttSignaler::~MqttSignaler() {
    try { unsubscribe(); } catch (...) {}
}

void MqttSignaler::publish(const std::uint8_t* data, std::size_t size) {
    connection_->publish(topic_, data, size, 0, false);
}

void MqttSignaler::subscribe(MessageCallback callback) {
    if (!callback) throw std::invalid_argument("MqttSignaler: callback is empty");
    unsubscribe();
    std::lock_guard<std::mutex> lock(mutex_);
    handle_ = connection_->addSubscription(topic_,
        [callback = std::move(callback)](const std::string&, const std::uint8_t* data,
                                         std::size_t size) { callback(data, size); }, 0);
}

void MqttSignaler::unsubscribe() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (handle_) {
        connection_->removeSubscription(topic_, handle_);
        handle_ = 0;
    }
}

void MqttSignaler::disconnect() { unsubscribe(); }

WebRtcConnection::WebRtcConnection(std::shared_ptr<MqttConnection> connection,
                                   const std::string& sessionId, WebRtcOptions options)
    : WebRtcConnection(std::make_shared<MqttSignaler>(std::move(connection), sessionId),
                       std::move(options)) {}

} // namespace magpie
