#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace magpie {

/** Carries opaque MAGPIE WebRTC signaling bytes for one session. */
class WebRtcSignaler {
public:
    using MessageCallback = std::function<void(const std::uint8_t*, std::size_t)>;

    virtual ~WebRtcSignaler() = default;
    virtual const std::string& sessionId() const noexcept = 0;
    virtual void publish(const std::uint8_t* data, std::size_t size) = 0;
    virtual void subscribe(MessageCallback callback) = 0;
    virtual void unsubscribe() = 0;
    virtual void disconnect() = 0;
};

} // namespace magpie
