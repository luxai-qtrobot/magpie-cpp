#pragma once

#include <magpie/transport/webrtc_signaler.hpp>

#include <atomic>
#include <deque>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace magpie {

/** ZeroMQ signaling: legacy PAIR or multi-client ROUTER/DEALER mode. */
class ZmqSignaler final : public WebRtcSignaler {
public:
    ZmqSignaler(std::string endpoint, std::string sessionId,
                bool bind = false, bool multiplex = false);
    ~ZmqSignaler() override;

    const std::string& sessionId() const noexcept override { return sessionId_; }
    void publish(const std::uint8_t* data, std::size_t size) override;
    void subscribe(MessageCallback callback) override;
    void unsubscribe() override;
    void disconnect() override;

private:
    void run(std::promise<void> ready);

    std::string endpoint_;
    std::string sessionId_;
    bool bind_;
    bool multiplex_;
    std::atomic<bool> stopped_{false};
    std::mutex mutex_;
    MessageCallback callback_;
    std::deque<std::vector<std::uint8_t>> outgoing_;
    std::thread worker_;
};

} // namespace magpie
