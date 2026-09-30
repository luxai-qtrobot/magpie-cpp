#include <magpie/transport/webrtc_connection.hpp>

#include <magpie/serializer/msgpack_serializer.hpp>
#include <magpie/transport/webrtc_http_signaler.hpp>
#include <magpie/utils/common.hpp>
#include <magpie/utils/logger.hpp>

#include <rtc/rtc.hpp>

#include <algorithm>
#include <condition_variable>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace magpie {

// ---------------------------------------------------------------------------
// WebRtcConnection::Impl
// ---------------------------------------------------------------------------

struct WebRtcConnection::Impl {
    // ---- Configuration ----
    std::shared_ptr<WebRtcSignaler> signaler;
    std::string                     sessionId;
    std::string                     peerId;
    WebRtcOptions                   options;
    std::shared_ptr<Serializer>     serializer;
    std::string                     targetRemote;
    std::string                     role{"mesh"};
    std::function<void(bool)>       onStateChange;


    // ---- WebRTC objects ----
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::DataChannel>    dc;
    std::shared_ptr<rtc::DataChannel>    mediaDc;    // "magpie-media" unreliable DC

    // ---- Connection state ----
    std::atomic<bool>       connected{false};
    std::atomic<bool>       disconnecting{false};
    std::mutex              connMutex;
    std::condition_variable connCv;

    // ---- Signaling / role negotiation ----
    std::mutex  roleMutex;
    std::string remotePeerId;
    bool        roleDecided{false};
    bool        remoteDescSet{false};

    // ---- Buffered ICE candidates (before remote description is set) ----
    std::mutex                                                        iceMutex;
    std::vector<std::tuple<std::string, std::string, int>>            pendingCandidates;
    // (candidate_str, sdpMid, sdpMLineIndex)

    // ---- Hello loop ----
    std::thread      helloThread;
    std::atomic<bool> helloStop{false};

    // ---- Pub callbacks: topic → handle → callback ----
    std::mutex              pubMutex;
    std::atomic<uint64_t>   nextPubHandle{1};
    std::unordered_map<std::string,
        std::unordered_map<uint64_t, DataCallback>> pubCallbacks;

    // ---- RPC request callbacks: service → handle → callback ----
    std::mutex              rpcReqMutex;
    std::atomic<uint64_t>   nextRpcReqHandle{1};
    std::unordered_map<std::string,
        std::unordered_map<uint64_t, RpcRequestCallback>> rpcReqCallbacks;

    // ---- RPC reply callbacks: rid → one-shot callback ----
    std::mutex              rpcRepMutex;
    std::unordered_map<std::string, RpcReplyCallback> rpcRepCallbacks;

    // ---- Video/audio callbacks (from magpie-media channel) ----
    std::mutex              mediaMutex;
    std::atomic<uint64_t>   nextMediaHandle{1};
    std::unordered_map<uint64_t, VideoCallback> videoCallbacks;
    std::unordered_map<uint64_t, AudioCallback> audioCallbacks;

    // ---- Methods ----

    void sendSignal(const Value::Dict& msg) {
        try {
            auto addressed = msg;
            if (!targetRemote.empty())
                addressed["to_peer_id"] = Value::fromString(targetRemote);
            addressed["role"] = Value::fromString(role);
            auto bytes = serializer->serialize(Value::fromDict(addressed));
            signaler->publish(bytes.data(), bytes.size());
        } catch (const std::exception& e) {
            Logger::warning("WebRtcConnection: signal send error: " + std::string(e.what()));
        }
    }

    void sendHello() {
        Value::Dict msg;
        msg["type"]    = Value::fromString("hello");
        msg["peer_id"] = Value::fromString(peerId);
        sendSignal(msg);
    }

    void setupPeerConnection() {
        rtc::Configuration config;

        // ICE servers
        for (const auto& ice : options.iceServers) {
            if (ice.username.empty()) {
                config.iceServers.emplace_back(ice.url);
            } else {
                rtc::IceServer srv(ice.url);
                srv.username = ice.username;
                srv.password = ice.password;
                config.iceServers.push_back(std::move(srv));
            }
        }

        // ICE transport policy
        if (options.iceTransportPolicy == "relay") {
            config.iceTransportPolicy = rtc::TransportPolicy::Relay;
        }

        pc = std::make_shared<rtc::PeerConnection>(config);

        // Capture shared_ptr to impl for callbacks; safe because PC is torn down
        // before impl_ is released, so impl will always outlive these callbacks.
        auto* impl = this;

        pc->onStateChange([impl](rtc::PeerConnection::State state) {
            if (impl->disconnecting.load()) return;
            Logger::debug("WebRtcConnection(" + impl->peerId + "): state → " +
                          std::to_string(static_cast<int>(state)));

            if (state == rtc::PeerConnection::State::Connected) {
                // Data channel open is authoritative; state==Connected just logs
            } else if (state == rtc::PeerConnection::State::Failed ||
                       state == rtc::PeerConnection::State::Disconnected ||
                       state == rtc::PeerConnection::State::Closed) {
                impl->connected.store(false);
                impl->connCv.notify_all();
                if (impl->onStateChange) impl->onStateChange(false);

                if (impl->options.reconnect && !impl->disconnecting.load()) {
                    Logger::info("WebRtcConnection(" + impl->peerId +
                                 "): connection lost — reconnecting...");
                    impl->scheduleReconnect();
                }
            }
        });

        pc->onLocalDescription([impl](rtc::Description desc) {
            if (impl->disconnecting.load()) return;
            const std::string type = desc.typeString();
            Logger::debug("WebRtcConnection(" + impl->peerId + "): local description ready (type=" + type + ")");
            Value::Dict msg;
            msg["type"]    = Value::fromString(type);
            msg["peer_id"] = Value::fromString(impl->peerId);
            msg["sdp"]     = Value::fromString(std::string(desc));
            impl->sendSignal(msg);
        });

        pc->onLocalCandidate([impl](rtc::Candidate candidate) {
            if (impl->disconnecting.load()) return;
            Value::Dict msg;
            msg["type"]          = Value::fromString("candidate");
            msg["peer_id"]       = Value::fromString(impl->peerId);
            msg["candidate"]     = Value::fromString(candidate.candidate());
            msg["sdpMid"]        = Value::fromString(candidate.mid());
            msg["sdpMLineIndex"] = Value::fromInt(0);
            impl->sendSignal(msg);
        });

        pc->onDataChannel([impl](std::shared_ptr<rtc::DataChannel> ch) {
            if (ch->label() == "magpie") {
                impl->dc = ch;
                impl->setupDataChannel(ch);
            } else if (ch->label() == "magpie-media" && impl->options.useMediaChannels) {
                // Accept magpie-media only when configured to use it
                impl->mediaDc = ch;
                impl->setupMediaChannel(ch);
            }
        });
    }

    void setupDataChannel(std::shared_ptr<rtc::DataChannel> ch) {
        auto* impl = this;

        ch->onOpen([impl]() {
            Logger::debug("WebRtcConnection(" + impl->peerId + "): data channel open.");
            impl->connected.store(true);
            impl->helloStop.store(true);
            impl->connCv.notify_all();
            if (impl->onStateChange) impl->onStateChange(true);
        });

        ch->onClosed([impl]() {
            Logger::debug("WebRtcConnection(" + impl->peerId + "): data channel closed.");
            impl->connected.store(false);
            impl->connCv.notify_all();
            if (impl->onStateChange) impl->onStateChange(false);

            if (impl->options.reconnect && !impl->disconnecting.load()) {
                impl->scheduleReconnect();
            }
        });

        ch->onMessage([impl](rtc::message_variant data) {
            if (!std::holds_alternative<rtc::binary>(data)) return;
            const auto& bytes = std::get<rtc::binary>(data);
            if (bytes.empty()) return;
            const uint8_t* p = reinterpret_cast<const uint8_t*>(bytes.data());
            impl->onDataChannelMessage(p, bytes.size());
        });
    }

    void setupMediaChannel(std::shared_ptr<rtc::DataChannel> ch) {
        auto* impl = this;

        ch->onOpen([impl]() {
            Logger::debug("WebRtcConnection(" + impl->peerId + "): media channel open.");
        });

        ch->onClosed([impl]() {
            Logger::debug("WebRtcConnection(" + impl->peerId + "): media channel closed.");
        });

        ch->onMessage([impl](rtc::message_variant data) {
            if (!std::holds_alternative<rtc::binary>(data)) return;
            const auto& bytes = std::get<rtc::binary>(data);
            if (bytes.empty()) return;
            const uint8_t* p = reinterpret_cast<const uint8_t*>(bytes.data());
            impl->onMediaChannelMessage(p, bytes.size());
        });
    }

    void createOffer() {
        Logger::debug("WebRtcConnection(" + peerId + "): creating offer.");

        // Data channel (offerer creates it)
        rtc::DataChannelInit dcInit;
        dcInit.reliability.unordered = !options.dataChannelOrdered;
        if (options.dataChannelMaxRetransmits >= 0) {
            dcInit.reliability.type  = rtc::Reliability::Type::Rexmit;
            dcInit.reliability.rexmit = static_cast<int>(options.dataChannelMaxRetransmits);
        }
        dc = pc->createDataChannel("magpie", dcInit);
        setupDataChannel(dc);

        // Create magpie-media unreliable DC only when useMediaChannels=true.
        // When false, video/audio goes through the reliable magpie DC instead.
        if (options.useMediaChannels) {
            rtc::DataChannelInit mediaDcInit;
            mediaDcInit.reliability.unordered = true;
            mediaDcInit.reliability.type      = rtc::Reliability::Type::Rexmit;
            mediaDcInit.reliability.rexmit    = 0;
            mediaDc = pc->createDataChannel("magpie-media", mediaDcInit);
            setupMediaChannel(mediaDc);
        }

        // Trigger SDP offer generation + ICE gathering
        pc->setLocalDescription();
    }

    void applyPendingCandidates() {
        std::unique_lock<std::mutex> lk(iceMutex);
        for (auto& [cand, mid, idx] : pendingCandidates) {
            try {
                pc->addRemoteCandidate(rtc::Candidate(cand, mid));
            } catch (const std::exception& e) {
                Logger::warning("WebRtcConnection: buffered ICE candidate error: " +
                                std::string(e.what()));
            }
        }
        pendingCandidates.clear();
    }

    void onSignalMessage(const uint8_t*     data,
                         std::size_t        size) {
        if (disconnecting.load()) return;

        Value envelope;
        try {
            envelope = serializer->deserialize(data, size);
        } catch (const std::exception& e) {
            Logger::warning("WebRtcConnection: failed to deserialize signal: " +
                            std::string(e.what()));
            return;
        }

        if (envelope.type() != Value::Type::Dict) return;
        const auto& d = envelope.asDict();

        auto itType   = d.find("type");
        auto itPeer   = d.find("peer_id");

        if (itType == d.end() || itType->second.type() != Value::Type::String) return;
        const std::string msgType  = itType->second.asString();

        // Ignore our own messages
        if (itPeer != d.end() && itPeer->second.type() == Value::Type::String) {
            if (itPeer->second.asString() == peerId) return;
        }

        // --- hello ---
        if (msgType == "hello") {
            if (itPeer == d.end() || itPeer->second.type() != Value::Type::String) return;
            const std::string remotePid = itPeer->second.asString();

            bool doOffer = false;
            bool doReply = false;
            {
                std::lock_guard<std::mutex> lk(roleMutex);
                if (remotePeerId.empty()) {
                    remotePeerId = remotePid;
                }
                if (!roleDecided) {
                    roleDecided = true;
                    if (peerId > remotePeerId) {
                        doOffer = true;
                        Logger::debug("WebRtcConnection(" + peerId + "): role = offerer");
                    } else {
                        doReply = true;
                        Logger::debug("WebRtcConnection(" + peerId + "): role = answerer");
                    }
                }
            }

            if (doOffer) {
                setupPeerConnection();
                createOffer();
            } else if (doReply) {
                // Send one hello back so the offerer can detect us
                sendHello();
                // Set up PC and wait for the offer
                setupPeerConnection();
            }
        }

        // --- offer ---
        else if (msgType == "offer") {
            // Ignore once the data channel is already open
            if (connected.load()) return;

            auto itSdp = d.find("sdp");
            if (itSdp == d.end() || itSdp->second.type() != Value::Type::String) return;
            const std::string sdp = itSdp->second.asString();

            Logger::debug("WebRtcConnection(" + peerId + "): received SDP offer.");

            // Ensure PC exists (offer may arrive before hello in some race conditions)
            if (!pc) {
                setupPeerConnection();
            }

            try {
                pc->setRemoteDescription(rtc::Description(sdp, "offer"));
                {
                    std::lock_guard<std::mutex> lk2(roleMutex);
                    remoteDescSet = true;
                }
                applyPendingCandidates();
                // setRemoteDescription(offer) causes libdatachannel to fire onLocalDescription
                // with the answer automatically — do NOT call setLocalDescription() here or it
                // will generate a second (offer) description and start an infinite renegotiation loop.
            } catch (const std::exception& e) {
                Logger::warning("WebRtcConnection: offer handling error: " + std::string(e.what()));
            }
        }

        // --- answer ---
        else if (msgType == "answer") {
            // Ignore once the data channel is already open
            if (connected.load()) return;

            auto itSdp = d.find("sdp");
            if (itSdp == d.end() || itSdp->second.type() != Value::Type::String) return;
            const std::string sdp = itSdp->second.asString();

            Logger::debug("WebRtcConnection(" + peerId + "): received SDP answer.");

            if (!pc) {
                Logger::warning("WebRtcConnection: received answer but no PC — ignoring.");
                return;
            }
            try {
                pc->setRemoteDescription(rtc::Description(sdp, "answer"));
                {
                    std::lock_guard<std::mutex> lk2(roleMutex);
                    remoteDescSet = true;
                }
                applyPendingCandidates();
            } catch (const std::exception& e) {
                Logger::warning("WebRtcConnection: answer handling error: " + std::string(e.what()));
            }
        }

        // --- candidate ---
        else if (msgType == "candidate") {
            auto itCand = d.find("candidate");
            auto itMid  = d.find("sdpMid");
            auto itIdx  = d.find("sdpMLineIndex");

            if (itCand == d.end() || itCand->second.type() != Value::Type::String) return;

            const std::string cand = itCand->second.asString();
            const std::string mid  = (itMid != d.end() && itMid->second.type() == Value::Type::String)
                                         ? itMid->second.asString() : "0";
            const int idx          = (itIdx != d.end() && itIdx->second.type() == Value::Type::Int)
                                         ? static_cast<int>(itIdx->second.asInt()) : 0;

            if (cand.empty()) return;

            bool remoteReady = false;
            {
                std::lock_guard<std::mutex> lk(roleMutex);
                remoteReady = remoteDescSet;
            }

            if (remoteReady && pc) {
                try {
                    pc->addRemoteCandidate(rtc::Candidate(cand, mid));
                } catch (const std::exception& e) {
                    Logger::warning("WebRtcConnection: ICE candidate error: " + std::string(e.what()));
                }
            } else {
                std::lock_guard<std::mutex> lk(iceMutex);
                pendingCandidates.emplace_back(cand, mid, idx);
            }
        }
    }

    void onDataChannelMessage(const uint8_t* data, std::size_t size) {
        Value msg;
        try {
            msg = serializer->deserialize(data, size);
        } catch (const std::exception& e) {
            Logger::warning("WebRtcConnection: data channel deserialize error: " +
                            std::string(e.what()));
            return;
        }

        if (msg.type() != Value::Type::Dict) return;
        const auto& d = msg.asDict();

        auto itType = d.find("type");
        if (itType == d.end() || itType->second.type() != Value::Type::String) return;
        const std::string msgType = itType->second.asString();

        if (msgType == "pub") {
            auto itTopic   = d.find("topic");
            auto itPayload = d.find("payload");
            if (itTopic == d.end() || itPayload == d.end()) return;
            if (itTopic->second.type() != Value::Type::String) return;

            const std::string& topic   = itTopic->second.asString();
            const Value&        payload = itPayload->second;

            std::vector<DataCallback> callbacks;
            {
                std::lock_guard<std::mutex> lk(pubMutex);
                auto it = pubCallbacks.find(topic);
                if (it != pubCallbacks.end()) {
                    for (auto& kv : it->second) {
                        callbacks.push_back(kv.second);
                    }
                }
            }
            for (auto& cb : callbacks) {
                try { cb(payload, topic); }
                catch (const std::exception& e) {
                    Logger::warning("WebRtcConnection: pub callback error for '" +
                                    topic + "': " + e.what());
                }
            }

        } else if (msgType == "rpc_req") {
            auto itService = d.find("service");
            if (itService == d.end() || itService->second.type() != Value::Type::String) return;
            const std::string& service = itService->second.asString();

            std::vector<RpcRequestCallback> callbacks;
            {
                std::lock_guard<std::mutex> lk(rpcReqMutex);
                auto it = rpcReqCallbacks.find(service);
                if (it != rpcReqCallbacks.end()) {
                    for (auto& kv : it->second) {
                        callbacks.push_back(kv.second);
                    }
                }
            }
            if (callbacks.empty()) {
                Logger::warning("WebRtcConnection: no handler for service '" + service + "'");
            }
            for (auto& cb : callbacks) {
                try { cb(msg); }
                catch (const std::exception& e) {
                    Logger::warning("WebRtcConnection: rpc_req callback error for '" +
                                    service + "': " + e.what());
                }
            }

        } else if (msgType == "media") {
            // Video/audio frame sent via the reliable data channel (useMediaChannels=false path).
            // "video"/"audio" topics route to video/audio callbacks; custom topics to pub callbacks.
            auto itTopic   = d.find("topic");
            auto itPayload = d.find("payload");
            if (itTopic == d.end() || itPayload == d.end()) return;
            if (itTopic->second.type() != Value::Type::String) return;
            if (itPayload->second.type() != Value::Type::Dict)   return;

            const std::string& topic   = itTopic->second.asString();
            const Value&        payload = itPayload->second;

            if (topic == "video") {
                std::vector<VideoCallback> callbacks;
                {
                    std::lock_guard<std::mutex> lk(mediaMutex);
                    for (auto& kv : videoCallbacks) callbacks.push_back(kv.second);
                }
                for (auto& cb : callbacks) {
                    try { cb(payload); }
                    catch (const std::exception& e) {
                        Logger::warning("WebRtcConnection: media video callback error: " +
                                        std::string(e.what()));
                    }
                }
            } else if (topic == "audio") {
                std::vector<AudioCallback> callbacks;
                {
                    std::lock_guard<std::mutex> lk(mediaMutex);
                    for (auto& kv : audioCallbacks) callbacks.push_back(kv.second);
                }
                for (auto& cb : callbacks) {
                    try { cb(payload); }
                    catch (const std::exception& e) {
                        Logger::warning("WebRtcConnection: media audio callback error: " +
                                        std::string(e.what()));
                    }
                }
            } else {
                // Custom topic — treat as pub data
                std::vector<DataCallback> callbacks;
                {
                    std::lock_guard<std::mutex> lk(pubMutex);
                    auto it = pubCallbacks.find(topic);
                    if (it != pubCallbacks.end()) {
                        for (auto& kv : it->second) callbacks.push_back(kv.second);
                    }
                }
                for (auto& cb : callbacks) {
                    try { cb(payload, topic); }
                    catch (const std::exception& e) {
                        Logger::warning("WebRtcConnection: media pub callback error for '" +
                                        topic + "': " + std::string(e.what()));
                    }
                }
            }

        } else if (msgType == "rpc_ack" || msgType == "rpc_rep") {
            auto itRid = d.find("rid");
            if (itRid == d.end() || itRid->second.type() != Value::Type::String) return;
            const std::string rid = itRid->second.asString();

            RpcReplyCallback cb;
            {
                std::lock_guard<std::mutex> lk(rpcRepMutex);
                auto it = rpcRepCallbacks.find(rid);
                if (it != rpcRepCallbacks.end()) {
                    cb = it->second;
                }
            }
            if (cb) {
                try { cb(msg); }
                catch (const std::exception& e) {
                    Logger::warning("WebRtcConnection: rpc reply callback error for rid='" +
                                    rid + "': " + e.what());
                }
            }
        }
    }

    void onMediaChannelMessage(const uint8_t* data, std::size_t size) {
        // magpie-media fallback path (useMediaChannels=true).
        // Wire format: {"kind":"video"|"audio", "topic":"...", "payload":<frame-dict>}
        // The "topic" field was added in a newer wire version; older peers omit it.
        Value msg;
        try {
            msg = serializer->deserialize(data, size);
        } catch (const std::exception& e) {
            Logger::warning("WebRtcConnection: media channel deserialize error: " +
                            std::string(e.what()));
            return;
        }

        if (msg.type() != Value::Type::Dict) return;
        const auto& d = msg.asDict();

        auto itKind    = d.find("kind");
        auto itPayload = d.find("payload");
        if (itKind    == d.end() || itKind->second.type()    != Value::Type::String) return;
        if (itPayload == d.end() || itPayload->second.type() != Value::Type::Dict)   return;

        const std::string& kind    = itKind->second.asString();
        const Value&        payload = itPayload->second;

        if (kind == "video") {
            std::vector<VideoCallback> callbacks;
            {
                std::lock_guard<std::mutex> lk(mediaMutex);
                for (auto& kv : videoCallbacks) callbacks.push_back(kv.second);
            }
            for (auto& cb : callbacks) {
                try { cb(payload); }
                catch (const std::exception& e) {
                    Logger::warning("WebRtcConnection: video callback error: " + std::string(e.what()));
                }
            }
        } else if (kind == "audio") {
            std::vector<AudioCallback> callbacks;
            {
                std::lock_guard<std::mutex> lk(mediaMutex);
                for (auto& kv : audioCallbacks) callbacks.push_back(kv.second);
            }
            for (auto& cb : callbacks) {
                try { cb(payload); }
                catch (const std::exception& e) {
                    Logger::warning("WebRtcConnection: audio callback error: " + std::string(e.what()));
                }
            }
        }
    }

    void scheduleReconnect() {
        // Tear down current PC and start fresh in a detached thread
        std::thread([this]() {
            // Small delay before reconnecting
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            if (disconnecting.load()) return;

            // Tear down
            if (pc) {
                try { pc->close(); } catch (...) {}
                pc.reset();
            }
            dc.reset();
            mediaDc.reset();

            // Reset negotiation state
            {
                std::lock_guard<std::mutex> lk(roleMutex);
                remotePeerId.clear();
                roleDecided   = false;
                remoteDescSet = false;
            }
            {
                std::lock_guard<std::mutex> lk(iceMutex);
                pendingCandidates.clear();
            }

            // New peer ID forces re-negotiation
            peerId = getUniqueId().substr(0, 16);
            Logger::debug("WebRtcConnection: reconnecting with new peerId=" + peerId);

            // Restart hello loop (30 s timeout)
            helloStop.store(false);
            if (helloThread.joinable()) helloThread.join();
            helloThread = std::thread([this]() { helloLoopFunc(30.0); });
        }).detach();
    }

    void helloLoopFunc(double timeoutSec) {
        const int maxTicks = std::max(1, static_cast<int>(timeoutSec));

        for (int i = 0; i < maxTicks; ++i) {
            if (helloStop.load() || disconnecting.load()) break;

            sendHello();

            std::unique_lock<std::mutex> lk(connMutex);
            connCv.wait_for(lk, std::chrono::seconds(1), [this]() {
                return helloStop.load() || connected.load() || disconnecting.load();
            });

            if (helloStop.load() || connected.load() || disconnecting.load()) break;
        }

        // Wake connect() so it can evaluate the result
        connCv.notify_all();
    }
};

namespace {

class DirectedSignaler final : public WebRtcSignaler {
public:
    DirectedSignaler(std::shared_ptr<WebRtcSignaler> parent, std::string session)
        : parent_(std::move(parent)), sessionId_(std::move(session)) {}

    const std::string& sessionId() const noexcept override { return sessionId_; }
    void publish(const std::uint8_t* data, std::size_t size) override {
        parent_->publish(data, size);
    }
    void subscribe(MessageCallback callback) override {
        std::deque<std::vector<std::uint8_t>> pending;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback_ = callback;
            pending.swap(pending_);
        }
        for (const auto& payload : pending) callback(payload.data(), payload.size());
    }
    void unsubscribe() override {
        std::lock_guard<std::mutex> lock(mutex_);
        callback_ = {};
    }
    void disconnect() override { unsubscribe(); }
    void deliver(const std::uint8_t* data, std::size_t size) {
        MessageCallback callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback = callback_;
            if (!callback) {
                pending_.emplace_back(data, data + size);
                return;
            }
        }
        if (callback) callback(data, size);
    }

private:
    std::shared_ptr<WebRtcSignaler> parent_;
    std::string sessionId_;
    std::mutex mutex_;
    MessageCallback callback_;
    std::deque<std::vector<std::uint8_t>> pending_;
};

} // namespace

struct WebRtcConnection::Group : std::enable_shared_from_this<Group> {
    std::shared_ptr<WebRtcSignaler> signaler;
    std::shared_ptr<Serializer> serializer{std::make_shared<MsgpackSerializer>()};
    WebRtcOptions options;
    std::string sessionId;
    std::string peerId{getUniqueId().substr(0, 16)};
    mutable std::mutex mutex;
    std::condition_variable cv;
    std::unordered_map<std::string, std::shared_ptr<WebRtcConnection>> peers;
    std::unordered_map<std::string, std::string> rpcOrigins;
    std::unordered_map<std::string,
        std::unordered_map<CallbackHandle, DataCallback>> pubCallbacks;
    std::unordered_map<std::string,
        std::unordered_map<CallbackHandle, RpcRequestCallback>> rpcReqCallbacks;
    std::unordered_map<std::string, RpcReplyCallback> rpcRepCallbacks;
    std::unordered_map<CallbackHandle, VideoCallback> videoCallbacks;
    std::unordered_map<CallbackHandle, AudioCallback> audioCallbacks;
    std::unordered_map<std::string, bool> everReadyPeers;
    std::unordered_map<std::string, std::string> restartIds;
    std::atomic<CallbackHandle> nextHandle{1};
    std::thread discoveryThread;
    bool started{false};
    bool stopped{false};
    bool everConnected{false};

    Group(std::shared_ptr<WebRtcSignaler> source, WebRtcOptions config)
        : signaler(std::move(source)), options(std::move(config)),
          sessionId(signaler->sessionId()) {}

    Value::Dict helloMessage() const {
        Value::Dict hello;
        hello["type"] = Value::fromString("hello");
        hello["peer_id"] = Value::fromString(peerId);
        hello["role"] = Value::fromString(options.role);
        return hello;
    }

    void sendHello(const std::string& target = {}, const std::string& restartId = {}) {
        auto hello = helloMessage();
        if (!target.empty()) hello["to_peer_id"] = Value::fromString(target);
        if (!restartId.empty()) hello["restart_id"] = Value::fromString(restartId);
        try {
            auto bytes = serializer->serialize(Value::fromDict(hello));
            signaler->publish(bytes.data(), bytes.size());
        } catch (const std::exception& e) {
            Logger::warning("WebRtcConnection: discovery send failed: " +
                            std::string(e.what()));
        }
    }

    void registerCallbacks(const std::shared_ptr<WebRtcConnection>& child,
                           const std::string& remote) {
        auto state = child->impl_;
        {
            std::lock_guard<std::mutex> lock(state->pubMutex);
            state->pubCallbacks = pubCallbacks;
        }
        {
            std::lock_guard<std::mutex> lock(state->rpcReqMutex);
            for (const auto& service : rpcReqCallbacks) {
                for (const auto& entry : service.second) {
                    auto weak = weak_from_this();
                    state->rpcReqCallbacks[service.first][entry.first] =
                        [weak, remote, callback=entry.second](const Value& message) {
                            if (auto group = weak.lock()) {
                                if (message.type() == Value::Type::Dict) {
                                    const auto& fields = message.asDict();
                                    auto rid = fields.find("rid");
                                    if (rid != fields.end() && rid->second.type() == Value::Type::String) {
                                        std::lock_guard<std::mutex> lock(group->mutex);
                                        group->rpcOrigins[rid->second.asString()] = remote;
                                    }
                                }
                            }
                            callback(message);
                        };
                }
            }
        }
        {
            std::lock_guard<std::mutex> lock(state->rpcRepMutex);
            state->rpcRepCallbacks = rpcRepCallbacks;
        }
        {
            std::lock_guard<std::mutex> lock(state->mediaMutex);
            state->videoCallbacks = videoCallbacks;
            state->audioCallbacks = audioCallbacks;
        }
    }

    void onSignal(const std::uint8_t* data, std::size_t size) {
        Value decoded;
        try { decoded = serializer->deserialize(data, size); }
        catch (const std::exception& e) {
            Logger::warning("WebRtcConnection: signal decode failed: " + std::string(e.what()));
            return;
        }
        if (decoded.type() != Value::Type::Dict) return;
        const auto& fields = decoded.asDict();
        auto from = fields.find("peer_id");
        if (from == fields.end() || from->second.type() != Value::Type::String) return;
        const std::string remote = from->second.asString();
        if (remote.empty() || remote == peerId) return;
        auto to = fields.find("to_peer_id");
        if (to != fields.end() &&
            (to->second.type() != Value::Type::String || to->second.asString() != peerId)) return;
        auto roleField = fields.find("role");
        std::string remoteRole = roleField != fields.end() &&
            roleField->second.type() == Value::Type::String
                ? roleField->second.asString() : "mesh";
        if ((options.role == "client" && remoteRole == "client") ||
            (options.role == "host" && remoteRole == "host")) return;

        std::shared_ptr<WebRtcConnection> child;
        std::shared_ptr<WebRtcConnection> oldChild;
        bool created = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stopped) return;
            auto found = peers.find(remote);
            auto typeField = fields.find("type");
            const bool isHello = typeField != fields.end() &&
                typeField->second.type() == Value::Type::String &&
                typeField->second.asString() == "hello";
            auto restartField = fields.find("restart_id");
            const bool newRestart = isHello && restartField != fields.end() &&
                restartField->second.type() == Value::Type::String &&
                restartIds[remote] != restartField->second.asString();
            if (newRestart) restartIds[remote] = restartField->second.asString();
            if (found != peers.end() && isHello &&
                (newRestart || (everReadyPeers.count(remote) && !found->second->isConnected()))) {
                oldChild = found->second;
                peers.erase(found);
                everReadyPeers.erase(remote);
                for (auto it = rpcOrigins.begin(); it != rpcOrigins.end();) {
                    if (it->second == remote) it = rpcOrigins.erase(it);
                    else ++it;
                }
                found = peers.end();
            }
            if (found == peers.end()) {
                auto adapter = std::make_shared<DirectedSignaler>(signaler, sessionId);
                auto childOptions = options;
                childOptions.reconnect = false;
                child.reset(new WebRtcConnection(adapter, childOptions, peerId));
                child->impl_->targetRemote = remote;
                child->impl_->role = options.role;
                auto weak = weak_from_this();
                child->impl_->onStateChange = [weak, remote](bool connected) {
                    if (auto group = weak.lock()) {
                        std::lock_guard<std::mutex> guard(group->mutex);
                        if (connected) {
                            group->everConnected = true;
                            group->everReadyPeers[remote] = true;
                        }
                        group->cv.notify_all();
                    }
                };
                registerCallbacks(child, remote);
                peers.emplace(remote, child);
                created = true;
            } else {
                child = found->second;
            }
        }
        if (created) child->startPeer();
        auto adapter = std::static_pointer_cast<DirectedSignaler>(child->impl_->signaler);
        adapter->deliver(data, size);
        if (oldChild) oldChild->disconnect();
    }

    void start() {
        if (auto http = std::dynamic_pointer_cast<HttpSignaler>(signaler)) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (started || stopped) return;
            }
            auto bytes = serializer->serialize(Value::fromDict(helloMessage()));
            http->announce(bytes);
        }
        std::lock_guard<std::mutex> lock(mutex);
        if (started || stopped) return;
        started = true;
        auto weak = weak_from_this();
        signaler->subscribe([weak](const std::uint8_t* bytes, std::size_t size) {
            if (auto group = weak.lock()) group->onSignal(bytes, size);
        });
        discoveryThread = std::thread([weak]() {
            while (auto group = weak.lock()) {
                bool sendDiscovery = false;
                {
                    std::lock_guard<std::mutex> lock(group->mutex);
                    if (group->stopped) break;
                    auto http = std::dynamic_pointer_cast<HttpSignaler>(group->signaler);
                    sendDiscovery = (!http || !http->supportsJoinAnnouncements()) &&
                        (!group->everConnected || group->options.reconnect);
                }
                if (sendDiscovery) group->sendHello();
                std::vector<std::pair<std::string, std::shared_ptr<WebRtcConnection>>> stale;
                {
                    std::unique_lock<std::mutex> lock(group->mutex);
                    for (auto it = group->peers.begin(); it != group->peers.end();) {
                        auto ready = group->everReadyPeers.find(it->first);
                        if (ready != group->everReadyPeers.end() && !it->second->isConnected()) {
                            stale.emplace_back(it->first, it->second);
                            group->everReadyPeers.erase(ready);
                            it = group->peers.erase(it);
                        } else ++it;
                    }
                    group->cv.wait_for(lock, std::chrono::seconds(1),
                                       [&]() { return group->stopped; });
                }
                for (auto& entry : stale) entry.second->disconnect();
                auto http = std::dynamic_pointer_cast<HttpSignaler>(group->signaler);
                if (http && http->supportsJoinAnnouncements() && group->options.reconnect) {
                    for (const auto& entry : stale)
                        group->sendHello(entry.first, getUniqueId());
                }
            }
        });
    }

    void stop() {
        std::vector<std::shared_ptr<WebRtcConnection>> children;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stopped) return;
            stopped = true;
            cv.notify_all();
        }
        signaler->unsubscribe();
        if (discoveryThread.joinable()) discoveryThread.join();
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (auto& entry : peers) children.push_back(entry.second);
            peers.clear();
            rpcOrigins.clear();
            restartIds.clear();
        }
        for (auto& child : children) child->disconnect();
        signaler->disconnect();
    }
};

// ---------------------------------------------------------------------------
// WebRtcConnection public API
// ---------------------------------------------------------------------------

WebRtcConnection::WebRtcConnection(std::shared_ptr<WebRtcSignaler> signaler,
                                    WebRtcOptions options)
{
    if (!signaler) {
        throw std::invalid_argument("WebRtcConnection: signaler is null");
    }
    if (signaler->sessionId().empty()) {
        throw std::invalid_argument("WebRtcConnection: session ID is empty");
    }
    if (options.role != "mesh" && options.role != "host" && options.role != "client") {
        throw std::invalid_argument("WebRtcConnection: role must be mesh, host, or client");
    }
    group_ = std::make_shared<Group>(std::move(signaler), std::move(options));
}

WebRtcConnection::WebRtcConnection(std::shared_ptr<WebRtcSignaler> signaler,
                                    WebRtcOptions options,
                                    const std::string& localPeerId)
    : impl_(std::make_shared<Impl>())
{
    impl_->sessionId   = signaler->sessionId();
    impl_->signaler    = std::move(signaler);
    impl_->options     = std::move(options);
    impl_->serializer  = std::make_shared<MsgpackSerializer>();
    impl_->peerId      = localPeerId;

    Logger::debug("WebRtcConnection: created, peerId=" + impl_->peerId +
                  ", sessionId=" + impl_->sessionId);
}

WebRtcConnection::~WebRtcConnection() {
    try { disconnect(); } catch (...) {}
}

void WebRtcConnection::startPeer() {
    auto weakImpl = std::weak_ptr<Impl>(impl_);
    impl_->signaler->subscribe([weakImpl](const uint8_t* data, std::size_t size) {
        if (auto state = weakImpl.lock()) state->onSignalMessage(data, size);
    });
    impl_->sendHello();
}

bool WebRtcConnection::connect(double timeoutSec) {
    if (group_) {
        group_->start();
        std::unique_lock<std::mutex> lock(group_->mutex);
        group_->cv.wait_for(lock, std::chrono::duration<double>(timeoutSec), [this]() {
            if (group_->stopped) return true;
            for (const auto& entry : group_->peers)
                if (entry.second->isConnected()) return true;
            return false;
        });
        for (const auto& entry : group_->peers)
            if (entry.second->isConnected()) return true;
        return false;
    }
    impl_->disconnecting.store(false);
    impl_->connected.store(false);
    impl_->helloStop.store(false);

    // Reset negotiation state for a fresh connect
    {
        std::lock_guard<std::mutex> lk(impl_->roleMutex);
        impl_->remotePeerId.clear();
        impl_->roleDecided   = false;
        impl_->remoteDescSet = false;
    }
    {
        std::lock_guard<std::mutex> lk(impl_->iceMutex);
        impl_->pendingCandidates.clear();
    }

    // Subscribe before sending the first hello.
    auto implPtr = impl_;
    std::weak_ptr<Impl> weakImpl = impl_;
    impl_->signaler->subscribe([weakImpl](const uint8_t* data, std::size_t size) {
        if (auto state = weakImpl.lock()) state->onSignalMessage(data, size);
    });

    // Start hello loop in background
    impl_->helloThread = std::thread([implPtr, timeoutSec]() {
        implPtr->helloLoopFunc(timeoutSec);
    });

    // Wait for data channel to open (or timeout + 1s grace)
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(timeoutSec + 2.0));

    {
        std::unique_lock<std::mutex> lk(impl_->connMutex);
        impl_->connCv.wait_until(lk, deadline, [this]() {
            return impl_->connected.load() ||
                   impl_->disconnecting.load() ||
                   !impl_->helloThread.joinable();
        });
    }

    // Stop the hello loop
    impl_->helloStop.store(true);
    impl_->connCv.notify_all();
    if (impl_->helloThread.joinable()) {
        impl_->helloThread.join();
    }

    if (!impl_->connected.load()) {
        Logger::warning("WebRtcConnection(" + impl_->peerId + "): connect timed out after " +
                        std::to_string(timeoutSec) + "s — no peer found");
    } else {
        Logger::info("WebRtcConnection(" + impl_->peerId + "): connected to " +
                     impl_->remotePeerId);
    }

    return impl_->connected.load();
}

void WebRtcConnection::disconnect() {
    if (group_) { group_->stop(); return; }
    if (impl_->disconnecting.exchange(true)) return;  // already disconnecting

    impl_->connected.store(false);
    impl_->helloStop.store(true);
    impl_->connCv.notify_all();

    // Join hello loop
    if (impl_->helloThread.joinable()) {
        impl_->helloThread.join();
    }

    // Stop the signaler after the hello loop has stopped.
    if (impl_->signaler) {
        try { impl_->signaler->unsubscribe(); }
        catch (const std::exception& e) {
            Logger::warning("WebRtcConnection: signaler unsubscribe error: " + std::string(e.what()));
        }
        try { impl_->signaler->disconnect(); }
        catch (const std::exception& e) {
            Logger::warning("WebRtcConnection: signaler disconnect error: " + std::string(e.what()));
        }
    }

    // Tear down WebRTC
    impl_->dc.reset();
    impl_->mediaDc.reset();
    if (impl_->pc) {
        try { impl_->pc->close(); } catch (...) {}
        impl_->pc.reset();
    }

    Logger::debug("WebRtcConnection(" + impl_->peerId + "): disconnected.");
}

bool WebRtcConnection::isConnected() const {
    if (group_) {
        std::lock_guard<std::mutex> lock(group_->mutex);
        for (const auto& entry : group_->peers)
            if (entry.second->isConnected()) return true;
        return false;
    }
    return impl_->connected.load();
}

const std::string& WebRtcConnection::peerId() const {
    if (group_) return group_->peerId;
    return impl_->peerId;
}

const std::string& WebRtcConnection::sessionId() const {
    if (group_) return group_->sessionId;
    return impl_->sessionId;
}

std::vector<std::string> WebRtcConnection::peerIds() const {
    std::vector<std::string> result;
    if (group_) {
        std::lock_guard<std::mutex> lock(group_->mutex);
        for (const auto& entry : group_->peers)
            if (entry.second->isConnected()) result.push_back(entry.first);
    } else if (impl_->connected.load()) {
        result.push_back(impl_->remotePeerId);
    }
    return result;
}

bool WebRtcConnection::useMediaChannels() const {
    if (group_) return group_->options.useMediaChannels;
    return impl_->options.useMediaChannels;
}

WebRtcConnection::CallbackHandle
WebRtcConnection::addPubCallback(const std::string& topic, DataCallback callback) {
    if (group_) {
        std::lock_guard<std::mutex> lock(group_->mutex);
        const auto handle = group_->nextHandle.fetch_add(1);
        group_->pubCallbacks[topic][handle] = callback;
        for (auto& entry : group_->peers) {
            auto state = entry.second->impl_;
            std::lock_guard<std::mutex> childLock(state->pubMutex);
            state->pubCallbacks[topic][handle] = callback;
        }
        return handle;
    }
    std::lock_guard<std::mutex> lk(impl_->pubMutex);
    const auto handle = impl_->nextPubHandle.fetch_add(1);
    impl_->pubCallbacks[topic][handle] = std::move(callback);
    return handle;
}

void WebRtcConnection::removePubCallback(const std::string& topic, CallbackHandle handle) {
    if (group_) {
        std::lock_guard<std::mutex> lock(group_->mutex);
        auto found = group_->pubCallbacks.find(topic);
        if (found != group_->pubCallbacks.end()) found->second.erase(handle);
        for (auto& entry : group_->peers) {
            auto state = entry.second->impl_;
            std::lock_guard<std::mutex> childLock(state->pubMutex);
            auto callbacks = state->pubCallbacks.find(topic);
            if (callbacks != state->pubCallbacks.end()) callbacks->second.erase(handle);
        }
        return;
    }
    std::lock_guard<std::mutex> lk(impl_->pubMutex);
    auto it = impl_->pubCallbacks.find(topic);
    if (it != impl_->pubCallbacks.end()) {
        it->second.erase(handle);
        if (it->second.empty()) impl_->pubCallbacks.erase(it);
    }
}

WebRtcConnection::CallbackHandle
WebRtcConnection::addRpcRequestCallback(const std::string& service, RpcRequestCallback callback) {
    if (group_) {
        std::lock_guard<std::mutex> lock(group_->mutex);
        const auto handle = group_->nextHandle.fetch_add(1);
        group_->rpcReqCallbacks[service][handle] = callback;
        auto weak = std::weak_ptr<Group>(group_);
        for (auto& entry : group_->peers) {
            const auto remote = entry.first;
            auto state = entry.second->impl_;
            std::lock_guard<std::mutex> childLock(state->rpcReqMutex);
            state->rpcReqCallbacks[service][handle] =
                [weak, remote, callback](const Value& message) {
                    if (auto group = weak.lock()) {
                        if (message.type() == Value::Type::Dict) {
                            const auto& fields = message.asDict();
                            auto rid = fields.find("rid");
                            if (rid != fields.end() && rid->second.type() == Value::Type::String) {
                                std::lock_guard<std::mutex> guard(group->mutex);
                                group->rpcOrigins[rid->second.asString()] = remote;
                            }
                        }
                    }
                    callback(message);
                };
        }
        return handle;
    }
    std::lock_guard<std::mutex> lk(impl_->rpcReqMutex);
    const auto handle = impl_->nextRpcReqHandle.fetch_add(1);
    impl_->rpcReqCallbacks[service][handle] = std::move(callback);
    return handle;
}

void WebRtcConnection::removeRpcRequestCallback(const std::string& service, CallbackHandle handle) {
    if (group_) {
        std::lock_guard<std::mutex> lock(group_->mutex);
        auto found = group_->rpcReqCallbacks.find(service);
        if (found != group_->rpcReqCallbacks.end()) found->second.erase(handle);
        for (auto& entry : group_->peers) {
            auto state = entry.second->impl_;
            std::lock_guard<std::mutex> childLock(state->rpcReqMutex);
            auto callbacks = state->rpcReqCallbacks.find(service);
            if (callbacks != state->rpcReqCallbacks.end()) callbacks->second.erase(handle);
        }
        return;
    }
    std::lock_guard<std::mutex> lk(impl_->rpcReqMutex);
    auto it = impl_->rpcReqCallbacks.find(service);
    if (it != impl_->rpcReqCallbacks.end()) {
        it->second.erase(handle);
        if (it->second.empty()) impl_->rpcReqCallbacks.erase(it);
    }
}

void WebRtcConnection::addRpcReplyCallback(const std::string& rid, RpcReplyCallback callback) {
    if (group_) {
        std::lock_guard<std::mutex> lock(group_->mutex);
        group_->rpcRepCallbacks[rid] = callback;
        for (auto& entry : group_->peers) {
            auto state = entry.second->impl_;
            std::lock_guard<std::mutex> childLock(state->rpcRepMutex);
            state->rpcRepCallbacks[rid] = callback;
        }
        return;
    }
    std::lock_guard<std::mutex> lk(impl_->rpcRepMutex);
    impl_->rpcRepCallbacks[rid] = std::move(callback);
}

void WebRtcConnection::removeRpcReplyCallback(const std::string& rid) {
    if (group_) {
        std::lock_guard<std::mutex> lock(group_->mutex);
        group_->rpcRepCallbacks.erase(rid);
        for (auto& entry : group_->peers) {
            auto state = entry.second->impl_;
            std::lock_guard<std::mutex> childLock(state->rpcRepMutex);
            state->rpcRepCallbacks.erase(rid);
        }
        return;
    }
    std::lock_guard<std::mutex> lk(impl_->rpcRepMutex);
    impl_->rpcRepCallbacks.erase(rid);
}

void WebRtcConnection::sendData(const Value& msg) {
    if (group_) {
        std::vector<std::shared_ptr<WebRtcConnection>> targets;
        {
            std::lock_guard<std::mutex> lock(group_->mutex);
            std::string kind;
            std::string rid;
            if (msg.type() == Value::Type::Dict) {
                const auto& fields = msg.asDict();
                auto type = fields.find("type");
                auto request = fields.find("rid");
                if (type != fields.end() && type->second.type() == Value::Type::String)
                    kind = type->second.asString();
                if (request != fields.end() && request->second.type() == Value::Type::String)
                    rid = request->second.asString();
            }
            if (kind == "rpc_ack" || kind == "rpc_rep") {
                auto origin = group_->rpcOrigins.find(rid);
                if (origin != group_->rpcOrigins.end()) {
                    auto peer = group_->peers.find(origin->second);
                    if (peer != group_->peers.end()) targets.push_back(peer->second);
                    if (kind == "rpc_rep") group_->rpcOrigins.erase(origin);
                }
            } else {
                for (const auto& entry : group_->peers)
                    if (entry.second->isConnected()) targets.push_back(entry.second);
            }
        }
        for (auto& peer : targets) peer->sendData(msg);
        return;
    }
    if (!impl_->connected.load() || !impl_->dc) return;
    try {
        auto bytes = impl_->serializer->serialize(msg);
        rtc::binary rtcBytes(reinterpret_cast<const std::byte*>(bytes.data()),
                             reinterpret_cast<const std::byte*>(bytes.data()) + bytes.size());
        impl_->dc->send(rtcBytes);
    } catch (const std::exception& e) {
        Logger::warning("WebRtcConnection: sendData error: " + std::string(e.what()));
    }
}

WebRtcConnection::CallbackHandle
WebRtcConnection::addVideoCallback(VideoCallback callback) {
    if (group_) {
        std::lock_guard<std::mutex> lock(group_->mutex);
        const auto handle = group_->nextHandle.fetch_add(1);
        group_->videoCallbacks[handle] = callback;
        for (auto& entry : group_->peers) {
            auto state = entry.second->impl_;
            std::lock_guard<std::mutex> childLock(state->mediaMutex);
            state->videoCallbacks[handle] = callback;
        }
        return handle;
    }
    std::lock_guard<std::mutex> lk(impl_->mediaMutex);
    const auto handle = impl_->nextMediaHandle.fetch_add(1);
    impl_->videoCallbacks[handle] = std::move(callback);
    return handle;
}

void WebRtcConnection::removeVideoCallback(CallbackHandle handle) {
    if (group_) {
        std::lock_guard<std::mutex> lock(group_->mutex);
        group_->videoCallbacks.erase(handle);
        for (auto& entry : group_->peers) {
            auto state = entry.second->impl_;
            std::lock_guard<std::mutex> childLock(state->mediaMutex);
            state->videoCallbacks.erase(handle);
        }
        return;
    }
    std::lock_guard<std::mutex> lk(impl_->mediaMutex);
    impl_->videoCallbacks.erase(handle);
}

WebRtcConnection::CallbackHandle
WebRtcConnection::addAudioCallback(AudioCallback callback) {
    if (group_) {
        std::lock_guard<std::mutex> lock(group_->mutex);
        const auto handle = group_->nextHandle.fetch_add(1);
        group_->audioCallbacks[handle] = callback;
        for (auto& entry : group_->peers) {
            auto state = entry.second->impl_;
            std::lock_guard<std::mutex> childLock(state->mediaMutex);
            state->audioCallbacks[handle] = callback;
        }
        return handle;
    }
    std::lock_guard<std::mutex> lk(impl_->mediaMutex);
    const auto handle = impl_->nextMediaHandle.fetch_add(1);
    impl_->audioCallbacks[handle] = std::move(callback);
    return handle;
}

void WebRtcConnection::removeAudioCallback(CallbackHandle handle) {
    if (group_) {
        std::lock_guard<std::mutex> lock(group_->mutex);
        group_->audioCallbacks.erase(handle);
        for (auto& entry : group_->peers) {
            auto state = entry.second->impl_;
            std::lock_guard<std::mutex> childLock(state->mediaMutex);
            state->audioCallbacks.erase(handle);
        }
        return;
    }
    std::lock_guard<std::mutex> lk(impl_->mediaMutex);
    impl_->audioCallbacks.erase(handle);
}

void WebRtcConnection::sendMediaFrame(const Value& msg) {
    if (group_) {
        std::vector<std::shared_ptr<WebRtcConnection>> targets;
        {
            std::lock_guard<std::mutex> lock(group_->mutex);
            for (const auto& entry : group_->peers)
                if (entry.second->isConnected()) targets.push_back(entry.second);
        }
        for (auto& peer : targets) peer->sendMediaFrame(msg);
        return;
    }
    if (!impl_->connected.load() || !impl_->mediaDc) return;
    try {
        auto bytes = impl_->serializer->serialize(msg);
        rtc::binary rtcBytes(reinterpret_cast<const std::byte*>(bytes.data()),
                             reinterpret_cast<const std::byte*>(bytes.data()) + bytes.size());
        impl_->mediaDc->send(rtcBytes);
    } catch (const std::exception& e) {
        Logger::warning("WebRtcConnection: sendMediaFrame error: " + std::string(e.what()));
    }
}

} // namespace magpie
