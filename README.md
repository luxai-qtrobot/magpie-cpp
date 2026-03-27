<p align="center">
  <img src="https://github.com/luxai-qtrobot/magpie-cpp/raw/main/assets/magpie.png" alt="MAGPIE Logo" width="200"/>
</p>

<h1 align="center">MAGPIE-CPP</h1>
<p align="center"><em>Message Abstraction & General-Purpose Integration Engine (C++)</em></p>

<p align="center">
  <a href="https://github.com/luxai-qtrobot/magpie-cpp/actions/workflows/ci.yml">
    <img src="https://github.com/luxai-qtrobot/magpie-cpp/actions/workflows/ci.yml/badge.svg?branch=main" alt="Test Status"/>
  </a>
</p>

---

MAGPIE-CPP is the C++ counterpart of the original **[MAGPIE (Python)](https://github.com/luxai-qtrobot/magpie)** project. It preserves the same core concepts, wire formats, and interoperability goals while offering a modern, efficient C++14 implementation suitable for embedded, robotics, and high-performance systems.

Originally developed at **[LuxAI](https://luxai.com)** for the [QTrobot](https://luxai.com/qtrobot-for-research/) ecosystem, MAGPIE-CPP is generic enough for any C++-based distributed system or AI pipeline.

---

## Features

- **Pub/Sub streaming** — high-throughput topic-based messaging via `StreamWriter` / `StreamReader`
- **Request/Response RPC** — synchronous RPC via `ZmqRpcRequester` / `ZmqRpcResponder` or `MqttRpcRequester` / `MqttRpcResponder`
- **Pluggable transports** — ZeroMQ and MQTT (paho); transport abstraction layer makes adding new backends straightforward
- **MQTT transport** — full pub/sub and RPC over MQTT with wildcard topics, TLS, auth, and auto-reconnect (optional)
- **WebRTC transport** — P2P pub/sub, video/audio streaming, and RPC over WebRTC; MQTT used for the initial signaling handshake, all payload traffic flows directly peer-to-peer; STUN + optional TURN for NAT traversal (optional)
- **Fast serialization** — msgpack by default; wire-compatible with Python MAGPIE
- **Typed frames** — `AudioFrameRaw`, `AudioFrameFlac`, `ImageFrameRaw`, `ImageFrameJpeg`, and more (optional)
- **Node helpers** — base classes (`BaseNode`, `SourceNode`, `SinkNode`, `ServerNode`, `ProcessNode`) for robust streaming services
- **Network discovery** — mDNS/Zeroconf node advertisement and scanning via Avahi (optional)
- **Lightweight core** — audio, video, MQTT, and discovery components are fully opt-in via CMake flags

---

## Installation

### Install from pre-built packages (.deb)

Pre-built Debian packages are available from the [Releases](https://github.com/luxai-qtrobot/magpie-cpp/releases) page.

Supported platforms:
- Debian 13 (arm64)
- Ubuntu 22.04 (amd64)
- Ubuntu 24.04 (amd64)

**Core library:**

```bash
# Ubuntu 24.04 amd64
sudo dpkg -i libmagpie_0.6.2-1deb24.04_amd64.deb

# Ubuntu 22.04 amd64
sudo dpkg -i libmagpie_0.6.2-1deb22.04_amd64.deb

# Debian 13 arm64
sudo dpkg -i libmagpie_0.6.2-1deb13_arm64.deb
```

**Audio extension (optional):**

```bash
# Ubuntu 24.04 amd64
sudo dpkg -i libmagpie-audio_0.6.2-1deb24.04_amd64.deb

# Ubuntu 22.04 amd64
sudo dpkg -i libmagpie-audio_0.6.2-1deb22.04_amd64.deb

# Debian 13 arm64
sudo dpkg -i libmagpie-audio_0.6.2-1deb13_arm64.deb
```

**MQTT extension (optional):**

```bash
# Ubuntu 24.04 amd64
sudo dpkg -i libmagpie-mqtt_0.6.2-1deb24.04_amd64.deb

# Ubuntu 22.04 amd64
sudo dpkg -i libmagpie-mqtt_0.6.2-1deb22.04_amd64.deb

# Debian 13 arm64
sudo dpkg -i libmagpie-mqtt_0.6.2-1deb13_arm64.deb
```

**Video extension (optional):**

```bash
# Ubuntu 24.04 amd64
sudo dpkg -i libmagpie-video_0.6.2-1deb24.04_amd64.deb

# Ubuntu 22.04 amd64
sudo dpkg -i libmagpie-video_0.6.2-1deb22.04_amd64.deb

# Debian 13 arm64
sudo dpkg -i libmagpie-video_0.6.2-1deb13_arm64.deb
```

---

### Build from Source

MAGPIE-CPP uses **CMake** and targets **C++14**.

Clone the repository:

```bash
git clone https://github.com/luxai-qtrobot/magpie-cpp.git
cd magpie-cpp
```

#### Core

Install dependencies:

```bash
sudo apt install libzmq3-dev libmsgpack-dev libfmt-dev
```

Build:

```bash
cmake -S . -B build
cmake --build build
```

#### With Audio support

Install additional dependency:

```bash
sudo apt install libzmq3-dev libmsgpack-dev libfmt-dev libflac-dev
```

Build:

```bash
cmake -S . -B build -DMAGPIE_WITH_AUDIO=ON
cmake --build build
```

#### With MQTT support

Install additional dependencies:

```bash
sudo apt install libzmq3-dev libmsgpack-dev libfmt-dev libpaho-mqtt-dev libpaho-mqttpp-dev
```

Build:

```bash
cmake -S . -B build -DMAGPIE_WITH_MQTT=ON
cmake --build build
```

The CMake library target is `magpie::mqtt`.

#### With Video support

Install additional dependency:

```bash
sudo apt install libzmq3-dev libmsgpack-dev libfmt-dev libturbojpeg0-dev
```

Build:

```bash
cmake -S . -B build -DMAGPIE_WITH_VIDEO=ON
cmake --build build
```

The CMake library target is `magpie::core`.

#### With WebRTC support

WebRTC transport requires MQTT support and [libdatachannel](https://github.com/paullouisageneau/libdatachannel).

Install libdatachannel (from source or a distro package, e.g.):

```bash
sudo apt install libdatachannel-dev   # or build from source
```

Build:

```bash
cmake -S . -B build -DMAGPIE_WITH_MQTT=ON -DMAGPIE_WITH_WEBRTC=ON
cmake --build build
```

The CMake library target is `magpie::webrtc`.

> **Note:** Zeroconf/mDNS discovery is always included in the core library — no extra flag or dependency needed. It uses a bundled mDNS implementation with standard POSIX sockets.

---

## Supported Platforms

- **C++ standard:** C++14
- **Linux** (x86\_64, ARM, Raspberry Pi, NVIDIA Jetson)

---

## Quick Start

### Pub/Sub

**Publisher:**

```cpp
#include <magpie/frames/primitive_frames.hpp>
#include <magpie/transport/zmq_publisher.hpp>
#include <magpie/utils/logger.hpp>

#include <chrono>
#include <thread>

int main() {
    using namespace magpie;

    ZmqPublisher pub("tcp://*:5555");
    int id = 0;

    while (true) {
        StringFrame frame("Hello " + std::to_string(id++));
        Logger::info("Publishing frame... ");
        pub.write(frame, "/mytopic");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
```

**Subscriber:**

```cpp
#include <magpie/frames/primitive_frames.hpp>
#include <magpie/transport/zmq_subscriber.hpp>
#include <magpie/transport/timeout_error.hpp>
#include <magpie/utils/logger.hpp>

int main() {
    using namespace magpie;

    ZmqSubscriber sub("tcp://127.0.0.1:5555", "/mytopic");

    while (true) {
        std::unique_ptr<Frame> frame;
        std::string topic;

        bool ok = sub.read(frame, topic, 3.0);
        if (!ok) {
            Logger::info("Subscriber: no frame (read returned false)");
            continue;
        }

        auto* tf = dynamic_cast<StringFrame*>(frame.get());
        Logger::info("Subscriber: got frame topic=" + topic + " value=" + tf->value());
    }
}
```

### Request / Response RPC

**Requester:**

```cpp
#include <magpie/transport/zmq_rpc_requester.hpp>
#include <magpie/serializer/value.hpp>
#include <magpie/utils/logger.hpp>

#include <iostream>

int main() {
    using namespace magpie;

    ZmqRpcRequester client("tcp://127.0.0.1:5556");

    Value::Dict request;
    request["message"] = Value::fromString("Hello from C++");
    request["count"]   = Value::fromInt(42);

    try {
        Logger::info("Sending RPC request...");
        Value response = client.call(Value::fromDict(request), 5.0);
        Logger::info("Received response: " + response.toDebugString());
    } catch (const TimeoutError& e) {
        Logger::error(std::string("RPC timeout: ") + e.what());
    } catch (const std::exception& e) {
        Logger::error(std::string("RPC error: ") + e.what());
    }

    client.close();
}
```

**Responder:**

```cpp
#include <magpie/transport/zmq_rpc_responder.hpp>
#include <magpie/transport/timeout_error.hpp>
#include <magpie/utils/logger.hpp>
#include <magpie/serializer/value.hpp>

#include <iostream>

int main() {
    using namespace magpie;

    ZmqRpcResponder server("tcp://*:5556");

    auto onRequest = [](const Value& req) -> Value {
        Logger::info("on_request:\n" + req.toDebugString());
        return req;
    };

    while (true) {
        try {
            bool ok = server.handleOnce(onRequest, 3.0);
            if (!ok) {
                Logger::info("responder: no request received.");
                continue;
            }
        } catch (const std::exception& e) {
            Logger::error(std::string("responder error: ") + e.what());
            break;
        }
    }
}
```

### MQTT Pub/Sub

All MQTT components share a single `MqttConnection` to the broker.

**Publisher:**

```cpp
#include <magpie/frames/primitive_frames.hpp>
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/mqtt_publisher.hpp>

int main() {
    using namespace magpie;

    auto conn = std::make_shared<MqttConnection>("mqtt://broker.hivemq.com:1883");
    conn->connect();

    MqttPublisher pub(conn);

    StringFrame frame("hello from C++");
    pub.write(frame, "sensors/temperature");

    pub.close();
    conn->disconnect();
}
```

**Subscriber** (supports `+` and `#` wildcards):

```cpp
#include <magpie/frames/primitive_frames.hpp>
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/mqtt_subscriber.hpp>

int main() {
    using namespace magpie;

    auto conn = std::make_shared<MqttConnection>("mqtt://broker.hivemq.com:1883");
    conn->connect();

    MqttSubscriber sub(conn, "sensors/+");

    std::unique_ptr<Frame> frame;
    std::string topic;
    if (sub.read(frame, topic, 5.0)) {
        auto* sf = dynamic_cast<StringFrame*>(frame.get());
        Logger::info("[" + topic + "] " + sf->value);
    }

    sub.close();
    conn->disconnect();
}
```

### MQTT RPC

**Requester:**

```cpp
#include <magpie/serializer/value.hpp>
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/mqtt_rpc_requester.hpp>

int main() {
    using namespace magpie;

    auto conn = std::make_shared<MqttConnection>("mqtt://broker.hivemq.com:1883");
    conn->connect();

    MqttRpcRequester req(conn, "robot/motion");

    Value::Dict d;
    d["action"] = Value::fromString("move");
    Value response = req.call(Value::fromDict(d), 5.0);

    req.close();
    conn->disconnect();
}
```

**Responder:**

```cpp
#include <magpie/serializer/value.hpp>
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/mqtt_rpc_responder.hpp>

int main() {
    using namespace magpie;

    auto conn = std::make_shared<MqttConnection>("mqtt://broker.hivemq.com:1883");
    conn->connect();

    MqttRpcResponder rsp(conn, "robot/motion");

    rsp.handleOnce([](const Value& req) -> Value {
        return Value::fromString("ok");
    }, /*timeoutSec=*/10.0);

    rsp.close();
    conn->disconnect();
}
```

### WebRTC Pub/Sub

WebRTC transport enables **P2P communication over the internet** — no broker in the data path after the initial handshake.  A `WebRtcConnection` is shared by all publishers, subscribers, and RPC components, mirroring the `MqttConnection` pattern.

Signaling (SDP offer/answer + ICE candidates) is exchanged via MQTT.  Role negotiation (offerer vs answerer) is fully automatic.

> **Note — no RTP media tracks in C++.**  The C++ implementation uses [libdatachannel](https://github.com/paullouisageneau/libdatachannel), which provides data channels only.  There is no RTP/SRTP media track stack (no equivalent of aiortc or a browser's `RTCPeerConnection` media pipeline).  Video and audio frames are always transported over WebRTC data channels — either the `magpie-media` unreliable channel (`useMediaChannels=true`, default) or the reliable `magpie` channel (`useMediaChannels=false`).  When interoperating with a Python or JS peer that has `use_media_channels=True` and sends real RTP video tracks, set `useMediaChannels=false` on the C++ side so that both peers agree to use the data-channel path for video/audio.

`WebRtcPublisher` routes internally based on frame type and the `useMediaChannels` option (default `true`):

| Frame type | `useMediaChannels=true` | `useMediaChannels=false` |
|---|---|---|
| `ImageFrameRaw` / `AudioFrameRaw` | `magpie-media` unreliable data channel | reliable `magpie` data channel (`{"type":"media","topic":"..."}`) |
| everything else | reliable `magpie` data channel | reliable `magpie` data channel |

With `useMediaChannels=false`, video and audio frames are topic-routed just like regular data, enabling **multiple simultaneous video/audio topics** (e.g. two cameras on different topics).

**Publisher:**

```cpp
#include <magpie/frames/image_frame.hpp>
#include <magpie/frames/primitive_frames.hpp>
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/webrtc_connection.hpp>
#include <magpie/transport/webrtc_publisher.hpp>

int main() {
    using namespace magpie;

    auto sig = std::make_shared<MqttConnection>("mqtt://broker.hivemq.com:1883");
    sig->connect();

    auto conn = std::make_shared<WebRtcConnection>(sig, "my-robot");
    if (!conn->connect(30.0)) {
        Logger::error("peer not found");
        return 1;
    }

    WebRtcPublisher pub(conn);

    // Send arbitrary data over the data channel
    DictFrame state;
    state["x"] = 1.0;
    pub.write(state, "robot/state");

    // Send a video frame over the magpie-media unreliable channel
    // (capture a real frame here; this is just a placeholder allocation)
    ImageFrameRaw img(width * height * 3, "raw", width, height, 3, "BGR");
    pub.write(img);   // topic defaults to "video"

    pub.close();
    conn->disconnect();
    sig->disconnect();
}
```

**Subscriber:**

```cpp
#include <magpie/frames/image_frame.hpp>
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/webrtc_connection.hpp>
#include <magpie/transport/webrtc_subscriber.hpp>

int main() {
    using namespace magpie;

    auto sig = std::make_shared<MqttConnection>("mqtt://broker.hivemq.com:1883");
    sig->connect();

    auto conn = std::make_shared<WebRtcConnection>(sig, "my-robot");
    if (!conn->connect(30.0)) { return 1; }

    // Subscribe to a data channel topic
    WebRtcSubscriber sub(conn, "robot/state");

    // Subscribe to video frames (magpie-media channel)
    WebRtcSubscriber vsub(conn, WebRtcSubscriber::VIDEO_TOPIC);

    while (true) {
        std::unique_ptr<Frame> frame;
        std::string topic;

        if (sub.read(frame, topic, 3.0)) {
            Logger::info("data: " + topic);
        }
        if (vsub.read(frame, topic, 0.0)) {
            auto* img = dynamic_cast<ImageFrameRaw*>(frame.get());
            Logger::info("video frame " + std::to_string(img->width()) +
                         "x" + std::to_string(img->height()));
        }
    }

    sub.close();
    vsub.close();
    conn->disconnect();
    sig->disconnect();
}
```

### WebRTC RPC

RPC over WebRTC uses the bidirectional data channel — no broker in the hot path, lower latency than MQTT RPC.

**Requester:**

```cpp
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/webrtc_connection.hpp>
#include <magpie/transport/webrtc_rpc_requester.hpp>

int main() {
    using namespace magpie;

    auto sig = std::make_shared<MqttConnection>("mqtt://broker.hivemq.com:1883");
    sig->connect();

    auto conn = std::make_shared<WebRtcConnection>(sig, "my-robot-rpc");
    if (!conn->connect(30.0)) { return 1; }

    WebRtcRpcRequester req(conn, "robot/motion");

    Value::Dict d;
    d["action"] = Value::fromString("move");
    d["x"]      = Value::fromFloat(1.0);

    try {
        Value response = req.call(Value::fromDict(d), 5.0);
        Logger::info("response: " + response.toDebugString());
    } catch (const TimeoutError& e) {
        Logger::error(std::string("timeout: ") + e.what());
    }

    req.close();
    conn->disconnect();
    sig->disconnect();
}
```

**Responder:**

```cpp
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/webrtc_connection.hpp>
#include <magpie/transport/webrtc_rpc_responder.hpp>

int main() {
    using namespace magpie;

    auto sig = std::make_shared<MqttConnection>("mqtt://broker.hivemq.com:1883");
    sig->connect();

    auto conn = std::make_shared<WebRtcConnection>(sig, "my-robot-rpc");
    if (!conn->connect(30.0)) { return 1; }

    WebRtcRpcResponder rsp(conn, "robot/motion");

    rsp.handleOnce([](const Value& req) -> Value {
        Logger::info("request: " + req.toDebugString());
        return Value::fromString("ok");
    }, /*timeoutSec=*/10.0);

    rsp.close();
    conn->disconnect();
    sig->disconnect();
}
```

### WebRTC Advanced Options

```cpp
#include <magpie/transport/webrtc_options.hpp>

WebRtcOptions opts;

// Disable STUN for pure-LAN use (faster connection, no external server needed)
opts.iceServers = {};

// Add a TURN relay server for strict NAT / corporate firewall scenarios
opts.iceServers.push_back({"turn:myturn.server:3478", "user", "pass"});
opts.iceTransportPolicy = "relay";   // force relay only

// Auto-reconnect when the peer connection drops
opts.reconnect = true;

// Route video/audio via the reliable data channel instead of magpie-media
// (useful when connecting to a peer without magpie-media support)
opts.useMediaChannels = false;

auto conn = std::make_shared<WebRtcConnection>(sig, "my-robot", opts);
```

#### MQTT URI schemes

| URI | Transport |
|-----|-----------|
| `mqtt://host:1883` | Plain TCP |
| `mqtts://host:8883` | TLS/TCP |
| `ws://host:9001/mqtt` | WebSocket |
| `wss://host:8884/mqtt` | TLS WebSocket |

#### Advanced options (TLS, auth, reconnect)

```cpp
MqttOptions opts;
opts.auth.mode     = "username_password";
opts.auth.username = "user";
opts.auth.password = "pass";
opts.tls.caFile    = "/etc/ssl/certs/ca-certificates.crt";
opts.reconnect.maxDelaySec = 60;

auto conn = std::make_shared<MqttConnection>("mqtts://broker.example.com:8883",
                                              "my-client-id", opts);
conn->connect();
```

### Network Discovery

**Advertise a node:**

```cpp
#include <magpie/discovery/zconf_discovery.hpp>
#include <magpie/utils/logger.hpp>
#include <magpie/utils/common.hpp>

#include <thread>
#include <chrono>

int main() {
    using namespace magpie;

    const std::string nodeId = getUniqueId();
    const std::uint16_t port = 5555;
    const std::string payload = R"({"role":"robot"})";

    ZconfDiscovery disc;
    disc.start();
    disc.advertise(nodeId, port, "zmq", payload);

    Logger::info("Advertising node_id=" + nodeId + " on port=" + std::to_string(port));

    try {
        while (true)
            std::this_thread::sleep_for(std::chrono::seconds(10));
    } catch (...) {}

    disc.close();
}
```

**Discover nodes:**

```cpp
#include <magpie/discovery/zconf_discovery.hpp>
#include <magpie/utils/logger.hpp>

#include <thread>
#include <chrono>

int main() {
    using namespace magpie;

    ZconfDiscovery disc;
    disc.start();

    try {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(2));

            auto nodes = disc.listNodes();
            if (nodes.empty()) {
                Logger::debug("No nodes discovered...");
                continue;
            }

            Logger::info("Discovered nodes:");
            for (const auto& info : nodes) {
                const std::string bestIp = ZconfDiscovery::pickBestIp(info.ips);
                Logger::info(
                    "  node_id=" + info.nodeId +
                    "  port=" + std::to_string(info.port) +
                    "  payload=" + info.payload +
                    "  (best ip: " + bestIp + ")"
                );
            }
        }
    } catch (...) {}

    disc.close();
}
```

---

## Architecture Overview

### Transports

| Transport | Pub/Sub | RPC | Package |
|-----------|---------|-----|---------|
| ZeroMQ | `ZmqPublisher` / `ZmqSubscriber` | `ZmqRpcRequester` / `ZmqRpcResponder` | core |
| MQTT (paho) | `MqttPublisher` / `MqttSubscriber` | `MqttRpcRequester` / `MqttRpcResponder` | `magpie::mqtt` |

The transport layer is **pluggable**: `StreamWriter`, `StreamReader`, `RpcRequester`, and `RpcResponder` are abstract base classes. Switching from ZMQ to MQTT (or any future transport) requires no changes to application-level frame or node code.

The MQTT transport shares a single `MqttConnection` per broker across all publishers, subscribers, and RPC components, reusing one TCP/TLS connection.

### Serialization

- `Serializer` abstract interface
- Msgpack implementation — wire-compatible with Python MAGPIE

### Node Helpers

Base classes for long-running processes: `BaseNode`, `ProcessNode`, `ServerNode`, `SourceNode`, `SinkNode`.
These simplify lifecycle management, threading, and clean shutdown.

### Frames

Typed containers for structured payloads:

- `Frame` (base class)
- Audio: `AudioFrameRaw`, `AudioFrameFlac` (requires `MAGPIE_WITH_AUDIO`)
- Image: `ImageFrameRaw`, `ImageFrameJpeg` (requires `MAGPIE_WITH_VIDEO`)

---

## Used in QTrobot

MAGPIE-CPP powers the internal messaging infrastructure of [QTrobot](https://luxai.com/qtrobot-for-research/) at **LuxAI**, handling audio/video streaming, distributed components, and SDK communication between robot subsystems.

---

## Project Status

**Status:** Beta — actively used in production-like systems. APIs are largely stable; minor changes are still possible.

**Roadmap:**
- ~~Additional transports (MQTT)~~ ✓ Done — `magpie::mqtt` sub-package
- Additional transports (WebRTC)
- Multi-transport support
- Higher-level pipeline abstractions for AI workloads

---

## Related Projects

| Project | Language | Repository |
|---|---|---|
| MAGPIE | Python | [luxai-qtrobot/magpie](https://github.com/luxai-qtrobot/magpie) |
| MAGPIE C++ | C++ (`libmagpie`, `libmagpie-mqtt`) | this repo |
| MAGPIE.js | TypeScript/JavaScript | [luxai-qtrobot/magpie-js](https://github.com/luxai-qtrobot/magpie-js) |

---

## License

Licensed under the [GNU General Public License v3 (GPLv3)](LICENSE).
