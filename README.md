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
sudo apt install libzmq3-dev libmsgpack-dev libfmt-dev \
                 libpaho-mqtt3as-dev libpaho-mqttpp3-dev
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

## License

Licensed under the [GNU General Public License v3 (GPLv3)](LICENSE).
