#pragma once

#include <string>

namespace magpie {

/**
 * TLS/SSL settings for MQTT connections.
 */
struct MqttTlsOptions {
    std::string caFile;             ///< Path to CA certificate file
    std::string certFile;           ///< Path to client certificate file (for mTLS)
    std::string keyFile;            ///< Path to client private key file (for mTLS)
    std::string keyPassword;        ///< Password for encrypted private key
    bool        verifyPeer{true};   ///< Verify broker certificate
    bool        verifyHostname{true}; ///< Verify hostname in certificate
};

/**
 * Authentication options for MQTT connections.
 */
struct MqttAuthOptions {
    /// Authentication mode: "none", "username_password", "mtls", "both"
    std::string mode{"none"};
    std::string username;
    std::string password;
};

/**
 * Session persistence options.
 */
struct MqttSessionOptions {
    bool cleanStart{true};      ///< MQTT clean session / clean start flag
    int  sessionExpirySec{0};   ///< MQTTv5 session expiry interval (0 = end on disconnect)
};

/**
 * Automatic reconnection settings.
 */
struct MqttReconnectOptions {
    bool enabled{true};
    int  minDelaySec{1};    ///< Minimum delay between reconnect attempts
    int  maxDelaySec{30};   ///< Maximum delay (exponential back-off cap)
};

/**
 * Last Will and Testament (LWT) options.
 */
struct MqttWillOptions {
    bool        enabled{false};
    std::string topic;
    std::string payload;
    int         qos{0};
    bool        retain{false};
};

/**
 * Default QoS and retain settings applied when not explicitly specified per call.
 */
struct MqttDefaultsOptions {
    int  publishQos{1};
    bool publishRetain{false};
    int  subscribeQos{1};
};

/**
 * Aggregate options for MqttConnection.
 *
 * All fields have sensible defaults; only override what you need.
 *
 * @code
 * MqttOptions opts;
 * opts.auth.mode     = "username_password";
 * opts.auth.username = "user";
 * opts.auth.password = "pass";
 * opts.reconnect.maxDelaySec = 60;
 * @endcode
 */
struct MqttOptions {
    MqttTlsOptions      tls;
    MqttAuthOptions     auth;
    MqttSessionOptions  session;
    MqttReconnectOptions reconnect;
    MqttWillOptions     will;
    MqttDefaultsOptions defaults;
};

} // namespace magpie
