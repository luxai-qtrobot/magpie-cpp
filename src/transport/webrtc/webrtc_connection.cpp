#include <magpie/transport/webrtc_connection.hpp>

#include <magpie/serializer/msgpack_serializer.hpp>
#include <magpie/utils/common.hpp>
#include <magpie/utils/logger.hpp>

#include <rtc/rtc.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
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
    std::shared_ptr<MqttConnection> signalConn;
    std::string                     sessionId;
    std::string                     peerId;
    std::string                     signalTopic;   // magpie/webrtc/<sessionId>/signal
    WebRtcOptions                   options;
    std::shared_ptr<Serializer>     serializer;

    MqttConnection::SubscriptionHandle signalHandle{0};

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
            auto bytes = serializer->serialize(Value::fromDict(msg));
            signalConn->publish(signalTopic, bytes.data(), bytes.size(), 0, false);
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
                config.iceServers.emplace_back(ice.url, ice.username, ice.password);
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
        });

        ch->onClosed([impl]() {
            Logger::debug("WebRtcConnection(" + impl->peerId + "): data channel closed.");
            impl->connected.store(false);
            impl->connCv.notify_all();

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
        dcInit.ordered = options.dataChannelOrdered;
        if (options.dataChannelMaxRetransmits >= 0) {
            dcInit.maxRetransmits = static_cast<unsigned int>(options.dataChannelMaxRetransmits);
        }
        dc = pc->createDataChannel("magpie", dcInit);
        setupDataChannel(dc);

        // Create magpie-media unreliable DC only when useMediaChannels=true.
        // When false, video/audio goes through the reliable magpie DC instead.
        if (options.useMediaChannels) {
            rtc::DataChannelInit mediaDcInit;
            mediaDcInit.ordered        = false;
            mediaDcInit.maxRetransmits = 0;
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

    void onSignalMessage(const std::string& /*topic*/,
                         const uint8_t*     data,
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
            auto itSdp = d.find("sdp");
            if (itSdp == d.end() || itSdp->second.type() != Value::Type::String) return;
            const std::string sdp = itSdp->second.asString();

            Logger::debug("WebRtcConnection(" + peerId + "): received SDP offer.");

            {
                std::lock_guard<std::mutex> lk(roleMutex);
                if (!pc) {
                    // Offer arrived before hello (race condition) — set up PC now
                    Logger::debug("WebRtcConnection(" + peerId + "): PC not ready, setting up for offer.");
                }
            }

            // Ensure PC exists
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
                // Answer SDP is generated automatically via onLocalDescription callback
                pc->setLocalDescription();
            } catch (const std::exception& e) {
                Logger::warning("WebRtcConnection: offer handling error: " + std::string(e.what()));
            }
        }

        // --- answer ---
        else if (msgType == "answer") {
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
            peerId = getUniqueId().substr(0, 12);
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

// ---------------------------------------------------------------------------
// WebRtcConnection public API
// ---------------------------------------------------------------------------

WebRtcConnection::WebRtcConnection(std::shared_ptr<MqttConnection> signalConn,
                                     const std::string&              sessionId,
                                     WebRtcOptions                   options)
    : impl_(std::make_shared<Impl>())
{
    impl_->signalConn  = std::move(signalConn);
    impl_->sessionId   = sessionId;
    impl_->options     = std::move(options);
    impl_->serializer  = std::make_shared<MsgpackSerializer>();
    impl_->signalTopic = "magpie/webrtc/" + sessionId + "/signal";
    impl_->peerId      = getUniqueId().substr(0, 12);

    if (!impl_->signalConn) {
        throw std::invalid_argument("WebRtcConnection: signalConn is null");
    }

    Logger::debug("WebRtcConnection: created, peerId=" + impl_->peerId +
                  ", sessionId=" + impl_->sessionId);
}

WebRtcConnection::~WebRtcConnection() {
    disconnect();
}

bool WebRtcConnection::connect(double timeoutSec) {
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

    // Subscribe to signaling topic
    auto implPtr = impl_;
    impl_->signalHandle = impl_->signalConn->addSubscription(
        impl_->signalTopic,
        [implPtr](const std::string& topic, const uint8_t* data, std::size_t size) {
            implPtr->onSignalMessage(topic, data, size);
        },
        0);

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
    if (impl_->disconnecting.exchange(true)) return;  // already disconnecting

    impl_->connected.store(false);
    impl_->helloStop.store(true);
    impl_->connCv.notify_all();

    // Join hello loop
    if (impl_->helloThread.joinable()) {
        impl_->helloThread.join();
    }

    // Remove signaling subscription
    if (impl_->signalConn && impl_->signalHandle != 0) {
        impl_->signalConn->removeSubscription(impl_->signalTopic, impl_->signalHandle);
        impl_->signalHandle = 0;
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
    return impl_->connected.load();
}

const std::string& WebRtcConnection::peerId() const {
    return impl_->peerId;
}

const std::string& WebRtcConnection::sessionId() const {
    return impl_->sessionId;
}

bool WebRtcConnection::useMediaChannels() const {
    return impl_->options.useMediaChannels;
}

WebRtcConnection::CallbackHandle
WebRtcConnection::addPubCallback(const std::string& topic, DataCallback callback) {
    std::lock_guard<std::mutex> lk(impl_->pubMutex);
    const auto handle = impl_->nextPubHandle.fetch_add(1);
    impl_->pubCallbacks[topic][handle] = std::move(callback);
    return handle;
}

void WebRtcConnection::removePubCallback(const std::string& topic, CallbackHandle handle) {
    std::lock_guard<std::mutex> lk(impl_->pubMutex);
    auto it = impl_->pubCallbacks.find(topic);
    if (it != impl_->pubCallbacks.end()) {
        it->second.erase(handle);
        if (it->second.empty()) impl_->pubCallbacks.erase(it);
    }
}

WebRtcConnection::CallbackHandle
WebRtcConnection::addRpcRequestCallback(const std::string& service, RpcRequestCallback callback) {
    std::lock_guard<std::mutex> lk(impl_->rpcReqMutex);
    const auto handle = impl_->nextRpcReqHandle.fetch_add(1);
    impl_->rpcReqCallbacks[service][handle] = std::move(callback);
    return handle;
}

void WebRtcConnection::removeRpcRequestCallback(const std::string& service, CallbackHandle handle) {
    std::lock_guard<std::mutex> lk(impl_->rpcReqMutex);
    auto it = impl_->rpcReqCallbacks.find(service);
    if (it != impl_->rpcReqCallbacks.end()) {
        it->second.erase(handle);
        if (it->second.empty()) impl_->rpcReqCallbacks.erase(it);
    }
}

void WebRtcConnection::addRpcReplyCallback(const std::string& rid, RpcReplyCallback callback) {
    std::lock_guard<std::mutex> lk(impl_->rpcRepMutex);
    impl_->rpcRepCallbacks[rid] = std::move(callback);
}

void WebRtcConnection::removeRpcReplyCallback(const std::string& rid) {
    std::lock_guard<std::mutex> lk(impl_->rpcRepMutex);
    impl_->rpcRepCallbacks.erase(rid);
}

void WebRtcConnection::sendData(const Value& msg) {
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
    std::lock_guard<std::mutex> lk(impl_->mediaMutex);
    const auto handle = impl_->nextMediaHandle.fetch_add(1);
    impl_->videoCallbacks[handle] = std::move(callback);
    return handle;
}

void WebRtcConnection::removeVideoCallback(CallbackHandle handle) {
    std::lock_guard<std::mutex> lk(impl_->mediaMutex);
    impl_->videoCallbacks.erase(handle);
}

WebRtcConnection::CallbackHandle
WebRtcConnection::addAudioCallback(AudioCallback callback) {
    std::lock_guard<std::mutex> lk(impl_->mediaMutex);
    const auto handle = impl_->nextMediaHandle.fetch_add(1);
    impl_->audioCallbacks[handle] = std::move(callback);
    return handle;
}

void WebRtcConnection::removeAudioCallback(CallbackHandle handle) {
    std::lock_guard<std::mutex> lk(impl_->mediaMutex);
    impl_->audioCallbacks.erase(handle);
}

void WebRtcConnection::sendMediaFrame(const Value& msg) {
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
