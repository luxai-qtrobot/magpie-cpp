#pragma once

#include <magpie/transport/webrtc_signaler.hpp>

#include <functional>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace magpie {

struct HttpSignalerOptions {
    using Headers = std::map<std::string, std::string>;

    Headers headers;
    // Called before every request; may run concurrently on send and poll threads.
    std::function<Headers()> headersProvider;
    std::string participantId;  // generated when empty
    std::string proxy;          // empty: use libcurl's environment defaults
    std::string caBundle;       // empty: use the system trust store
    double pollWait{20.0};
    double requestTimeout{10.0};
};

/** HTTP long-poll signaler for the Python/Node MAGPIE relay protocol. */
class HttpSignaler final : public WebRtcSignaler {
public:
    HttpSignaler(std::string baseUrl, std::string sessionId,
                 HttpSignalerOptions options = {});
    ~HttpSignaler() override;

    const std::string& sessionId() const noexcept override;
    /// Register an opaque hello in the PUT body; true when the relay caches it.
    bool announce(const std::vector<std::uint8_t>& payload);
    bool supportsJoinAnnouncements() const noexcept;
    void publish(const std::uint8_t* data, std::size_t size) override;
    void subscribe(MessageCallback callback) override;
    void unsubscribe() override;
    void disconnect() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace magpie
