<p align="center">
  <img src="https://github.com/luxai-qtrobot/magpie-cpp/raw/main/assets/magpie.png" alt="MAGPIE Logo" width="200"/>
</p>

# MAGPIE-CPP – Message Abstraction & General-Purpose Integration Engine (C++)

> **MAGPIE-CPP is a lightweight, modular C++ messaging engine providing high-performance pub/sub and RPC over pluggable transports.**

![Test Status](https://github.com/luxai-qtrobot/magpie-cpp/actions/workflows/ci.yml/badge.svg?branch=main)


MAGPIE-CPP is the C++ counterpart of the original **MAGPIE (Python)** project.  
It preserves the same core concepts, wire formats, and interoperability goals, while offering a modern, efficient C++14 implementation suitable for embedded, robotics, and high-performance systems.

Originally built for **QTrobot** at LuxAI, MAGPIE-CPP is generic enough to be used in any C++-based distributed system or AI pipeline.

---

## Features

- 📨 **High-level messaging API**
  - Stream-oriented **pub/sub** (`StreamWriter`, `StreamReader`)
  - **RPC** request/response (`RpcRequester`, `RpcResponder`)

- 🔌 **Pluggable transports**
  - ZeroMQ-based implementations (ZMQ Publisher / Subscriber / RPC)
  - Transport abstraction layer for future backends

- 📦 **Serialization abstraction**
  - Abstract `Serializer` interface
  - Msgpack-based serializer (wire-compatible with Python MAGPIE)

- 🧱 **Node helper classes**
  - `BaseNode`, `ProcessNode`, `ServerNode`, `SourceNode`, `SinkNode`
  - Clean lifecycle management and thread-safe shutdown

- 🧊 **Typed frames**
  - Core `Frame` base class
  - Audio frames: `AudioFrameRaw`, `AudioFrameFlac` (optional)
  - Image frames: `ImageFrameRaw`, `ImageFrameJpeg` (optional)

- 🌐 **Zeroconf / mDNS discovery**
  - Avahi-based node discovery on Linux
  - Interoperable with Python MAGPIE Zeroconf discovery

- 🧩 **Optional components via CMake options**
  - Core library stays lightweight
  - Audio, video, and discovery are opt-in

---

## Build & Installation

MAGPIE-CPP uses **CMake** and targets **C++14**.

### Requirements

- C++14 compatible compiler
- CMake ≥ 3.10
- ZeroMQ (libzmq3-dev)
- Msgpack-C (libmsgpack-dev)
- {fmt} (libfmt-dev)

Optional dependencies:
- **Audio**: Flac encoder (libflac-dev)
- **Video**: Jpeg encoder (libturbojpeg0-dev/libturbojpeg-dev)

### CMake options

```cmake
option(MAGPIE_WITH_AUDIO      "Build audio-related components"      OFF)
option(MAGPIE_WITH_VIDEO      "Build video-related components"      OFF)
```

### Build example

```bash
git clone https://github.com/luxai-qtrobot/magpie-cpp.git
cd magpie-cpp

cmake -S . -B build \
  -DMAGPIE_WITH_AUDIO=ON \
  -DMAGPIE_WITH_VIDEO=ON \  

cmake --build build
```

The library target is:

```cmake
magpie::core
```

---

## Supported environment

- **C++ standard:** C++14
- **OS / platforms (tested)**
  - Linux (x86_64, ARM)
  - Raspberry Pi / NVIDIA Jetson

---

## Quick Start Examples

### Pub/Sub – Publisher

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

### Pub/Sub – Subscriber

```cpp
#include <magpie/frames/primitive_frames.hpp>
#include <magpie/transport/zmq_subscriber.hpp>
#include <magpie/transport/timeout_error.hpp>
#include <magpie/utils/logger.hpp>

int main() {
    using namespace magpie;

    // Subscribe to /mytopic on localhost
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
        Logger::info("Subscriber: got TestFrame topic=" + topic + " value=" + tf->value());
    }
}
```

---

## RPC Example

### Requester

```cpp
#include <magpie/transport/zmq_rpc_requester.hpp>
#include <magpie/serializer/value.hpp>
#include <magpie/utils/logger.hpp>

#include <iostream>

int main() {
    using namespace magpie;

    // Connect to a ZMQRpcResponder endpoint
    ZmqRpcRequester client("tcp://127.0.0.1:5556");

    // Build request object with desired payload
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

### Responder

```cpp
#include <magpie/transport/zmq_rpc_responder.hpp>
#include <magpie/transport/timeout_error.hpp>
#include <magpie/utils/logger.hpp>
#include <magpie/serializer/value.hpp>

#include <iostream>

int main() {
    using namespace magpie;

    // Bind responder to an endpoint
    ZmqRpcResponder server("tcp://*:5556");

    // Handler: echo request back
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
        }
        catch (const std::exception& e) {
            Logger::error(std::string("responder error: ") + e.what());
            break;
        }
    }
}

```

---

## Zeroconf / mDNS Discovery (Optional)

MAGPIE-CPP supports **local network discovery** using mDNS/DNS-SD via **Avahi**.

- Advertise nodes with metadata (node id, protocol, JSON payload)
- Discover and resolve other MAGPIE nodes on the same network
- Interoperable with Python MAGPIE discovery

Example of advertising using mDNS:

```cpp
#include <magpie/discovery/zconf_discovery.hpp>
#include <magpie/utils/logger.hpp>
#include <magpie/utils/common.hpp>

#include <iostream>
#include <thread>
#include <chrono>

int main() {
    using namespace magpie;

    const std::string nodeId = getUniqueId();
    const std::uint16_t port = 5555;
    const std::string payload = R"({"hello":"world"})";

    Logger::info("Advertising node_id=" + nodeId + " on port=" + std::to_string(port) + " ...");

    ZconfDiscovery disc;
    disc.start();

    disc.advertise( nodeId, port, "zmq", payload );

    try {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
        }
    } catch (...) {}

    Logger::info("Advertiser shutting down...");
    disc.close();
}
```


Example of scanning using mDNS:

```cpp
#include <magpie/discovery/zconf_discovery.hpp>
#include <magpie/utils/logger.hpp>
#include <magpie/utils/common.hpp>

#include <iostream>
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
                    "  ips=" + (info.ips.empty() ? "[]" : info.ips.front()) +
                    "  port=" + std::to_string(info.port) +
                    "  payload=" + info.payload +
                    "  (best: " + bestIp + ")"
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

- ZeroMQ Publisher / Subscriber
- ZeroMQ RPC Requester / Responder

The transport layer is **pluggable** and isolated from user code.

### Serialization

- `Serializer` abstract interface
- Msgpack implementation
- Wire-compatible with Python MAGPIE

### Nodes

Helper classes for long-running processes:

- `BaseNode`
- `ProcessNode`
- `ServerNode`
- `SourceNode`, `SinkNode`

These simplify lifecycle management, threading, and shutdown.

### Frames

Typed containers for structured payloads:

- `Frame` (base class)
- Audio: `AudioFrameRaw`, `AudioFrameFlac`
- Image: `ImageFrameRaw`, `ImageFrameJpeg`

---

## Used in QTrobot

MAGPIE-CPP is used internally at **LuxAI** as part of the QTrobot ecosystem for:

- Distributed components
- Audio/video streaming
- SDK and middleware integration

---

## Project status

- **Status:** Beta
- APIs are largely stable
- Actively used and tested with Python MAGPIE interoperability

---

## License

This project is licensed under the **GNU General Public License v3 (GPLv3)**.  
See the `LICENSE` file for details.
