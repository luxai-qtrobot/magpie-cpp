#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/utils/logger.hpp>

#include <mqtt/async_client.h>
#include <mqtt/connect_options.h>
#include <mqtt/ssl_options.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace magpie {

// ---------------------------------------------------------------------------
// URI parsing: convert our scheme to paho's scheme
// ---------------------------------------------------------------------------

static std::string convertUri(const std::string& uri) {
    // mqtt://  -> tcp://
    // mqtts:// -> ssl://
    // ws://    -> ws://    (no change)
    // wss://   -> wss://   (no change)
    if (uri.compare(0, 7, "mqtt://") == 0) {
        return "tcp://" + uri.substr(7);
    }
    if (uri.compare(0, 8, "mqtts://") == 0) {
        return "ssl://" + uri.substr(8);
    }
    // ws:// and wss:// are already valid for paho
    return uri;
}

// ---------------------------------------------------------------------------
// MQTT topic wildcard matching (MQTT spec §4.7)
// ---------------------------------------------------------------------------

static bool topicMatches(const std::string& filter, const std::string& topic) {
    // Fast path: exact match
    if (filter == topic) return true;

    const char* f = filter.c_str();
    const char* t = topic.c_str();

    while (*f && *t) {
        if (*f == '+') {
            // '+' matches one level: advance t to next '/' or end
            while (*t && *t != '/') ++t;
            ++f; // consume '+'
        } else if (*f == '#') {
            return true; // '#' matches everything that remains
        } else if (*f == *t) {
            ++f;
            ++t;
        } else {
            return false;
        }
    }

    // Handle trailing '#' in filter
    if (*f == '#' && (f == filter.c_str() || *(f - 1) == '/')) {
        return true;
    }

    return *f == '\0' && *t == '\0';
}

// ---------------------------------------------------------------------------
// Paho callback class (private, defined in this TU only)
// ---------------------------------------------------------------------------

class MqttConnectionCallback : public mqtt::callback {
public:
    // owner pointer; the callback object must not outlive the connection
    struct Owner {
        virtual void onConnected()                                             = 0;
        virtual void onConnectionLost(const std::string& cause)               = 0;
        virtual void onMessage(mqtt::const_message_ptr msg)                   = 0;
        virtual ~Owner() = default;
    };

    explicit MqttConnectionCallback(Owner* owner) : owner_(owner) {}

    void connected(const std::string& /*cause*/) override {
        owner_->onConnected();
    }

    void connection_lost(const std::string& cause) override {
        owner_->onConnectionLost(cause);
    }

    void message_arrived(mqtt::const_message_ptr msg) override {
        owner_->onMessage(msg);
    }

private:
    Owner* owner_;
};

// ---------------------------------------------------------------------------
// MqttConnection::Impl
// ---------------------------------------------------------------------------

struct MqttConnection::Impl : public MqttConnectionCallback::Owner {
    // ---- data ----
    std::string  uri;
    std::string  clientId;
    MqttOptions  options;

    std::unique_ptr<mqtt::async_client>         client;
    std::unique_ptr<MqttConnectionCallback>     callback;

    // subscription registry
    struct TopicEntry {
        int qos{1};
        std::unordered_map<SubscriptionHandle, MessageCallback> callbacks;
    };
    mutable std::mutex                            subMutex;
    std::unordered_map<std::string, TopicEntry>   subscriptions;
    std::atomic<SubscriptionHandle>               nextHandle{1};

    // ---- Owner interface ----

    void onConnected() override {
        Logger::info("MqttConnection: connected to " + uri);
        // Re-subscribe to all registered topics
        std::unordered_map<std::string, int> toSubscribe;
        {
            std::lock_guard<std::mutex> lk(subMutex);
            for (auto& kv : subscriptions) {
                toSubscribe[kv.first] = kv.second.qos;
            }
        }
        for (auto& kv : toSubscribe) {
            try {
                client->subscribe(kv.first, kv.second)->wait_for(std::chrono::seconds(5));
            } catch (const std::exception& e) {
                Logger::warning("MqttConnection: re-subscribe failed for '" +
                                kv.first + "': " + e.what());
            }
        }
    }

    void onConnectionLost(const std::string& cause) override {
        Logger::warning("MqttConnection: connection lost – " +
                        (cause.empty() ? std::string("(no cause given)") : cause));
    }

    void onMessage(mqtt::const_message_ptr msg) override {
        if (!msg) return;

        const std::string& topic = msg->get_topic();
        const auto&        raw   = msg->get_payload();
        const uint8_t* data = reinterpret_cast<const uint8_t*>(raw.data());
        std::size_t    size = raw.size();

        // Collect matching callbacks outside the lock to avoid calling user code
        // while holding the mutex.
        std::vector<MessageCallback> toCall;
        {
            std::lock_guard<std::mutex> lk(subMutex);
            for (auto& kv : subscriptions) {
                if (topicMatches(kv.first, topic)) {
                    for (auto& cb : kv.second.callbacks) {
                        toCall.push_back(cb.second);
                    }
                }
            }
        }

        for (auto& cb : toCall) {
            try {
                cb(topic, data, size);
            } catch (const std::exception& e) {
                Logger::warning("MqttConnection: callback threw: " + std::string(e.what()));
            } catch (...) {
                Logger::warning("MqttConnection: callback threw unknown exception");
            }
        }
    }
};

// ---------------------------------------------------------------------------
// MqttConnection public API
// ---------------------------------------------------------------------------

MqttConnection::MqttConnection(const std::string& uri,
                                 const std::string& clientId,
                                 MqttOptions        options)
    : impl_(new Impl{})
{
    impl_->uri      = convertUri(uri);
    impl_->options  = std::move(options);

    // Generate a client ID if not provided
    if (clientId.empty()) {
        // Simple unique ID: "magpie-" + random-ish suffix
        impl_->clientId = "magpie-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
    } else {
        impl_->clientId = clientId;
    }

    impl_->client   = std::make_unique<mqtt::async_client>(impl_->uri, impl_->clientId);
    impl_->callback = std::make_unique<MqttConnectionCallback>(impl_.get());
    impl_->client->set_callback(*impl_->callback);

    Logger::debug("MqttConnection: created for " + impl_->uri +
                  " (clientId=" + impl_->clientId + ")");
}

MqttConnection::~MqttConnection() {
    try {
        disconnect();
    } catch (...) {}
}

void MqttConnection::connect(double timeoutSec) {
    mqtt::connect_options opts;

    // Session
    opts.set_clean_session(impl_->options.session.cleanStart);

    // Reconnect
    if (impl_->options.reconnect.enabled) {
        opts.set_automatic_reconnect(
            static_cast<unsigned int>(impl_->options.reconnect.minDelaySec),
            static_cast<unsigned int>(impl_->options.reconnect.maxDelaySec));
    }

    // Auth
    const auto& auth = impl_->options.auth;
    if (auth.mode == "username_password" || auth.mode == "both") {
        opts.set_user_name(auth.username);
        opts.set_password(auth.password);
    }

    // TLS
    const auto& tls = impl_->options.tls;
    bool needsTls = (impl_->uri.compare(0, 6, "ssl://") == 0 ||
                     impl_->uri.compare(0, 6, "wss://") == 0);
    if (needsTls || !tls.caFile.empty() || !tls.certFile.empty()) {
        mqtt::ssl_options ssl;
        if (!tls.caFile.empty())   ssl.set_trust_store(tls.caFile);
        if (!tls.certFile.empty()) ssl.set_key_store(tls.certFile);
        if (!tls.keyFile.empty())  ssl.set_private_key(tls.keyFile);
        if (!tls.keyPassword.empty()) ssl.set_private_key_password(tls.keyPassword);
        ssl.set_verify(tls.verifyPeer);
        opts.set_ssl(ssl);
    }

    // Will
    if (impl_->options.will.enabled) {
        opts.set_will(mqtt::message(
            impl_->options.will.topic,
            impl_->options.will.payload,
            impl_->options.will.qos,
            impl_->options.will.retain));
    }

    Logger::debug("MqttConnection: connecting to " + impl_->uri + " ...");
    try {
        auto tok = impl_->client->connect(opts);
        const auto timeoutMs = static_cast<long long>(timeoutSec * 1000.0);
        if (!tok->wait_for(std::chrono::milliseconds(timeoutMs))) {
            throw std::runtime_error("MqttConnection: connect timed out after " +
                                     std::to_string(timeoutSec) + "s");
        }
    } catch (const mqtt::exception& e) {
        throw std::runtime_error(std::string("MqttConnection: connect failed: ") + e.what());
    }
}

void MqttConnection::disconnect() {
    if (!impl_->client) return;
    try {
        if (impl_->client->is_connected()) {
            impl_->client->disconnect()->wait_for(std::chrono::seconds(5));
            Logger::debug("MqttConnection: disconnected from " + impl_->uri);
        }
    } catch (const mqtt::exception& e) {
        Logger::warning("MqttConnection: disconnect error: " + std::string(e.what()));
    }
}

void MqttConnection::publish(const std::string& topic,
                              const uint8_t*     data,
                              std::size_t        size,
                              int                qos,
                              bool               retain) {
    if (!impl_->client) {
        Logger::warning("MqttConnection::publish: client is null");
        return;
    }

    const int resolvedQos = (qos < 0) ? impl_->options.defaults.publishQos : qos;
    const bool resolvedRetain = retain || impl_->options.defaults.publishRetain;

    try {
        auto msg = mqtt::make_message(
            topic,
            reinterpret_cast<const char*>(data),
            size,
            resolvedQos,
            resolvedRetain);
        impl_->client->publish(msg)->wait_for(std::chrono::seconds(5));
    } catch (const mqtt::exception& e) {
        Logger::warning("MqttConnection: publish error on '" + topic + "': " +
                        std::string(e.what()));
    }
}

MqttConnection::SubscriptionHandle
MqttConnection::addSubscription(const std::string& topicFilter,
                                  MessageCallback    callback,
                                  int                qos) {
    const int resolvedQos = (qos < 0) ? impl_->options.defaults.subscribeQos : qos;

    SubscriptionHandle handle = impl_->nextHandle.fetch_add(1);

    bool isFirst = false;
    {
        std::lock_guard<std::mutex> lk(impl_->subMutex);
        auto& entry = impl_->subscriptions[topicFilter];
        isFirst = entry.callbacks.empty();
        if (isFirst) entry.qos = resolvedQos;
        entry.callbacks[handle] = std::move(callback);
    }

    if (isFirst && impl_->client && impl_->client->is_connected()) {
        try {
            impl_->client->subscribe(topicFilter, resolvedQos)->wait_for(std::chrono::seconds(5));
            Logger::debug("MqttConnection: subscribed to '" + topicFilter + "'");
        } catch (const mqtt::exception& e) {
            Logger::warning("MqttConnection: subscribe error for '" + topicFilter + "': " +
                            std::string(e.what()));
        }
    }

    return handle;
}

void MqttConnection::removeSubscription(const std::string& topicFilter,
                                         SubscriptionHandle  handle) {
    bool shouldUnsub = false;
    {
        std::lock_guard<std::mutex> lk(impl_->subMutex);
        auto it = impl_->subscriptions.find(topicFilter);
        if (it != impl_->subscriptions.end()) {
            it->second.callbacks.erase(handle);
            if (it->second.callbacks.empty()) {
                impl_->subscriptions.erase(it);
                shouldUnsub = true;
            }
        }
    }

    if (shouldUnsub && impl_->client && impl_->client->is_connected()) {
        try {
            impl_->client->unsubscribe(topicFilter)->wait_for(std::chrono::seconds(5));
            Logger::debug("MqttConnection: unsubscribed from '" + topicFilter + "'");
        } catch (const mqtt::exception& e) {
            Logger::warning("MqttConnection: unsubscribe error for '" + topicFilter + "': " +
                            std::string(e.what()));
        }
    }
}

bool MqttConnection::isConnected() const {
    return impl_->client && impl_->client->is_connected();
}

int MqttConnection::defaultPublishQos() const noexcept {
    return impl_->options.defaults.publishQos;
}

int MqttConnection::defaultSubscribeQos() const noexcept {
    return impl_->options.defaults.subscribeQos;
}

bool MqttConnection::defaultPublishRetain() const noexcept {
    return impl_->options.defaults.publishRetain;
}

} // namespace magpie
