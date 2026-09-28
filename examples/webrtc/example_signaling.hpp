#pragma once

#include <magpie/transport/webrtc_http_signaler.hpp>
#include <magpie/transport/webrtc_zmq_signaler.hpp>
#ifdef MAGPIE_WITH_MQTT
#include <magpie/transport/webrtc_mqtt_signaler.hpp>
#include <magpie/transport/mqtt_connection.hpp>
#endif

#include <memory>
#include <stdexcept>
#include <string>

inline std::shared_ptr<magpie::WebRtcSignaler> exampleSignaler(
    const std::string& address, const std::string& session, bool bind = false) {
    if (address.rfind("http://", 0) == 0 || address.rfind("https://", 0) == 0)
        return std::make_shared<magpie::HttpSignaler>(address, session);
    if (address.rfind("tcp://", 0) == 0 || address.rfind("ipc://", 0) == 0)
        return std::make_shared<magpie::ZmqSignaler>(address, session, bind);
#ifdef MAGPIE_WITH_MQTT
    if (address.rfind("mqtt://", 0) == 0 || address.rfind("mqtts://", 0) == 0 ||
        address.rfind("ws://", 0) == 0 || address.rfind("wss://", 0) == 0) {
        auto connection = std::make_shared<magpie::MqttConnection>(address);
        connection->connect(10.0);
        return std::make_shared<magpie::MqttSignaler>(connection, session);
    }
#endif
    throw std::invalid_argument("Unsupported signaling address: " + address);
}
