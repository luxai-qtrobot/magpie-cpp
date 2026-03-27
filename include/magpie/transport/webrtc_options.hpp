#pragma once

#include <string>
#include <vector>

namespace magpie {

/**
 * ICE server configuration (STUN or TURN).
 */
struct WebRtcIceServer {
    std::string url;       ///< e.g. "stun:stun.l.google.com:19302" or "turn:host:3478"
    std::string username;  ///< Only required for TURN servers
    std::string password;  ///< Only required for TURN servers
};

/**
 * Configuration options for WebRtcConnection.
 *
 * All fields have sensible defaults; only override what you need.
 *
 * @code
 * WebRtcOptions opts;
 * opts.iceServers = {};  // disable STUN — purely local testing
 * opts.reconnect = true;
 * @endcode
 */
struct WebRtcOptions {
    /// ICE servers used for NAT traversal.
    /// Defaults to Google's public STUN server.
    std::vector<WebRtcIceServer> iceServers{
        WebRtcIceServer{"stun:stun.l.google.com:19302"}
    };

    /// ICE transport policy: "all" (default) or "relay" (force TURN only).
    std::string iceTransportPolicy{"all"};

    /// Data channel ordered delivery guarantee (default: true).
    bool dataChannelOrdered{true};

    /// Data channel max retransmits (-1 = unlimited / fully reliable).
    int dataChannelMaxRetransmits{-1};

    /// Automatically re-establish the peer connection when it drops.
    bool reconnect{false};

    /**
     * Use the "magpie-media" unreliable data channel for video/audio frames
     * (default: true).
     *
     * Set to false when the remote peer does not support magpie-media — e.g.
     * when connecting to a Python/JS peer configured with
     * use_media_channels=False.  In that mode video/audio frames are sent on
     * the reliable "magpie" data channel with:
     *   { "type":"media", "topic":"...", "payload":<frame-dict> }
     * which mirrors the Python/JS use_media_channels=False wire format.
     */
    bool useMediaChannels{true};
};

} // namespace magpie
