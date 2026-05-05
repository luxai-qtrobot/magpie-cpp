<p align="center">
  <img src="https://github.com/luxai-qtrobot/magpie-cpp/raw/main/assets/magpie.png" alt="MAGPIE Logo" width="200"/>
</p>

<h1 align="center">MAGPIE C++</h1>
<p align="center"><em>Message Abstraction &amp; General-Purpose Integration Engine (C++)</em></p>

<p align="center">
  <a href="https://github.com/luxai-qtrobot/magpie-cpp/actions/workflows/ci.yml">
    <img src="https://github.com/luxai-qtrobot/magpie-cpp/actions/workflows/ci.yml/badge.svg?branch=main" alt="CI Status"/>
  </a>
  <a href="https://github.com/luxai-qtrobot/magpie-cpp/blob/main/LICENSE">
    <img src="https://img.shields.io/github/license/luxai-qtrobot/magpie-cpp" alt="License"/>
  </a>
</p>

---

MAGPIE C++ is the C++ counterpart of **[MAGPIE (Python)](https://github.com/luxai-qtrobot/magpie)** — a **transport-agnostic messaging and RPC framework for developers and AI agents**.

Whether the wire is ZeroMQ, MQTT, WebRTC, or something entirely custom, the application layer never changes. Services built with MAGPIE C++ are natively consumable by AI tools via built-in MCP support, and fully interoperable with Python MAGPIE and MAGPIE.js across all transports.

---

## Table of Contents

- [Features](#features)
- [Installation](#installation)
- [Quick Start](#quick-start)
  - [ZMQ Streaming](#zmq-streaming)
  - [ZMQ Request / Response RPC](#zmq-request--response-rpc)
  - [MQTT Streaming](#mqtt-streaming)
  - [MQTT Request / Response RPC](#mqtt-request--response-rpc)
  - [MQTT Advanced Options](#mqtt-advanced-options)
  - [WebRTC Streaming](#webrtc-streaming)
  - [WebRTC Request / Response RPC](#webrtc-request--response-rpc)
  - [WebRTC Advanced Options](#webrtc-advanced-options)
  - [Schema-based RPC](#schema-based-rpc)
  - [MCP Integration](#mcp-integration)
  - [Network Discovery](#network-discovery)
- [Architecture](#architecture)
- [Related Projects](#related-projects)
- [License](#license)

---

## Features

- **One API, any transport** — `StreamWriter`, `StreamReader`, `RpcRequester`, `RpcResponder` work identically over ZMQ, MQTT, and WebRTC; swap transports with one constructor change
- **Topic-based streaming** — high-throughput pub/sub via typed frames; publishers and subscribers are completely decoupled
- **Request / Response RPC** — synchronous request/reply with ACK, timeout, and per-call demux over any transport
- **Schema-based RPC** — JSON-RPC 2.0 dispatch via `JsonRpcSchema`; define your API once, call methods by name (`client.call("add", {{"a", 3}, {"b", 4}})`)
- **MCP server support** — `McpSchema` turns any MAGPIE C++ RPC responder into a fully compliant MCP tool server (`initialize`, `tools/list`, `tools/call`); any FastMCP `Client` using the Python `McpTransport` can call those tools over ZMQ, MQTT, or WebRTC
- **MQTT transport** — full streaming and RPC over MQTT; shared connection; supports `mqtt://`, `mqtts://`, `ws://`, `wss://`, TLS, auth, LWT, and auto-reconnect
- **WebRTC transport** — P2P streaming and RPC over WebRTC; MQTT used only for the initial signaling handshake; all payload traffic flows peer-to-peer; STUN + optional TURN for NAT traversal
- **Typed frames** — `ImageFrameJpeg`, `ImageFrameRaw`, `AudioFrameRaw`, `AudioFrameFlac`, and more; automatic serialization/deserialization across all transports
- **Node helpers** — `BaseNode`, `SourceNode`, `SinkNode`, `ProcessNode`, `ServerNode` add lifecycle and thread management on top of the raw transport primitives
- **Network discovery** — mDNS/Zeroconf node advertisement and scanning via `ZconfDiscovery`
- **Interoperable** — wire-compatible with Python MAGPIE and MAGPIE.js across all transports and serialization formats
- **Lightweight core** — ZeroMQ is the only core dependency; MQTT, WebRTC, audio, and video are fully opt-in

---

## Installation

### Pre-built packages (.deb)

Pre-built Debian packages are available from the [Releases](https://github.com/luxai-qtrobot/magpie-cpp/releases) page.

**Supported platforms:** Debian 13 (arm64), Ubuntu 22.04 (amd64), Ubuntu 24.04 (amd64)

| Package | What it adds |
|---|---|
| `libmagpie` | Core library — ZMQ streaming, RPC, schema, MCP, nodes, discovery |
| `libmagpie-audio` | Audio frames (`AudioFrameRaw`, `AudioFrameFlac`) |
| `libmagpie-mqtt` | MQTT transport |
| `libmagpie-video` | Image frames (`ImageFrameRaw`, `ImageFrameJpeg`) |

```bash
# Ubuntu 24.04 amd64
sudo dpkg -i libmagpie_0.6.2-1deb24.04_amd64.deb

# Ubuntu 22.04 amd64
sudo dpkg -i libmagpie_0.6.2-1deb22.04_amd64.deb

# Debian 13 arm64
sudo dpkg -i libmagpie_0.6.2-1deb13_arm64.deb
```

### Build from Source

MAGPIE C++ uses **CMake** and targets **C++14**. Supported on Linux (x86\_64, ARM, Raspberry Pi, NVIDIA Jetson).

```bash
git clone https://github.com/luxai-qtrobot/magpie-cpp.git
cd magpie-cpp
```

| Option | Extra dependency | CMake flag |
|---|---|---|
| Core (ZMQ + Schema + MCP) | `libzmq3-dev libmsgpack-dev libfmt-dev` | _(default)_ |
| Audio frames | `libflac-dev` | `-DMAGPIE_WITH_AUDIO=ON` |
| MQTT transport | `libpaho-mqtt-dev libpaho-mqttpp-dev` | `-DMAGPIE_WITH_MQTT=ON` |
| Video frames | `libturbojpeg0-dev` | `-DMAGPIE_WITH_VIDEO=ON` |
| WebRTC transport | `libdatachannel-dev` + MQTT | `-DMAGPIE_WITH_MQTT=ON -DMAGPIE_WITH_WEBRTC=ON` |

```bash
# Core only
sudo apt install libzmq3-dev libmsgpack-dev libfmt-dev
cmake -S . -B build && cmake --build build

# With MQTT
sudo apt install libpaho-mqtt-dev libpaho-mqttpp-dev
cmake -S . -B build -DMAGPIE_WITH_MQTT=ON && cmake --build build

# With WebRTC (requires MQTT)
sudo apt install libdatachannel-dev
cmake -S . -B build -DMAGPIE_WITH_MQTT=ON -DMAGPIE_WITH_WEBRTC=ON && cmake --build build
```

> Network discovery (mDNS/Zeroconf) is always included in the core library — no extra flag or dependency needed.

---

## Quick Start

### ZMQ Streaming

**Writer:**

```cpp
#include <magpie/frames/primitive_frames.hpp>
#include <magpie/transport/zmq_stream_writer.hpp>
#include <magpie/utils/logger.hpp>

#include <chrono>
#include <thread>

int main() {
    using namespace magpie;

    ZmqStreamWriter writer("tcp://*:5555");
    int id = 0;
    while (true) {
        StringFrame frame("Hello " + std::to_string(id++));
        writer.write(frame, "/mytopic");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
```

**Reader:**

```cpp
#include <magpie/frames/primitive_frames.hpp>
#include <magpie/transport/zmq_stream_reader.hpp>
#include <magpie/utils/logger.hpp>

int main() {
    using namespace magpie;

    ZmqStreamReader reader("tcp://127.0.0.1:5555", "/mytopic");
    while (true) {
        std::unique_ptr<Frame> frame;
        std::string topic;
        if (reader.read(frame, topic, /*timeoutSec=*/3.0)) {
            auto* sf = dynamic_cast<StringFrame*>(frame.get());
            Logger::info(topic + ": " + sf->value());
        }
    }
}
```

---

### ZMQ Request / Response RPC

**Responder:**

```cpp
#include <magpie/transport/zmq_rpc_responder.hpp>
#include <magpie/utils/logger.hpp>

int main() {
    using namespace magpie;

    ZmqRpcResponder server("tcp://*:5556");
    while (true) {
        try {
            server.handleOnce([](const Value& req) -> Value {
                Logger::info("request: " + req.toDebugString());
                return req;  // echo
            }, /*timeoutSec=*/1.0);
        } catch (const std::exception& e) {
            Logger::error(std::string("error: ") + e.what());
            break;
        }
    }
}
```

**Requester:**

```cpp
#include <magpie/transport/zmq_rpc_requester.hpp>
#include <magpie/transport/timeout_error.hpp>
#include <magpie/utils/logger.hpp>

int main() {
    using namespace magpie;

    ZmqRpcRequester client("tcp://127.0.0.1:5556");

    Value::Dict req;
    req["action"] = Value::fromString("greet");
    req["name"]   = Value::fromString("Bob");

    try {
        Value response = client.call(Value::fromDict(req), /*timeoutSec=*/3.0);
        Logger::info("response: " + response.toDebugString());
    } catch (const TimeoutError& e) {
        Logger::error(std::string("timeout: ") + e.what());
    }

    client.close();
}
```

---

### MQTT Streaming

MQTT uses a **shared connection** — create it once, pass it to any number of writers, readers, and RPC components.

**Writer:**

```cpp
#include <magpie/frames/primitive_frames.hpp>
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/mqtt_stream_writer.hpp>

int main() {
    using namespace magpie;

    auto conn = std::make_shared<MqttConnection>("mqtt://broker.hivemq.com:1883");
    conn->connect();

    MqttStreamWriter writer(conn);
    StringFrame frame("hello from C++");
    writer.write(frame, "sensors/temperature");

    writer.close();
    conn->disconnect();
}
```

**Reader** (supports `+` and `#` wildcards):

```cpp
#include <magpie/frames/primitive_frames.hpp>
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/mqtt_stream_reader.hpp>

int main() {
    using namespace magpie;

    auto conn = std::make_shared<MqttConnection>("mqtt://broker.hivemq.com:1883");
    conn->connect();

    MqttStreamReader reader(conn, "sensors/+");
    std::unique_ptr<Frame> frame;
    std::string topic;
    if (reader.read(frame, topic, /*timeoutSec=*/5.0)) {
        auto* sf = dynamic_cast<StringFrame*>(frame.get());
        Logger::info("[" + topic + "] " + sf->value());
    }

    reader.close();
    conn->disconnect();
}
```

---

### MQTT Request / Response RPC

**Responder:**

```cpp
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/mqtt_rpc_responder.hpp>

int main() {
    using namespace magpie;

    auto conn = std::make_shared<MqttConnection>("mqtt://broker.hivemq.com:1883");
    conn->connect();

    MqttRpcResponder server(conn, "myservice/actions");
    while (true) {
        server.handleOnce([](const Value& req) -> Value {
            return Value::fromString("ok");
        }, /*timeoutSec=*/1.0);
    }

    conn->disconnect();
}
```

**Requester:**

```cpp
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/mqtt_rpc_requester.hpp>
#include <magpie/transport/timeout_error.hpp>

int main() {
    using namespace magpie;

    auto conn = std::make_shared<MqttConnection>("mqtt://broker.hivemq.com:1883");
    conn->connect();

    MqttRpcRequester client(conn, "myservice/actions");
    try {
        Value::Dict d;
        d["action"] = Value::fromString("move");
        Value response = client.call(Value::fromDict(d), /*timeoutSec=*/5.0);
        Logger::info("response: " + response.toDebugString());
    } catch (const TimeoutError& e) {
        Logger::error(std::string("timeout: ") + e.what());
    }

    client.close();
    conn->disconnect();
}
```

---

### MQTT Advanced Options

```cpp
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/mqtt_options.hpp>

MqttOptions opts;
opts.auth.mode     = "username_password";
opts.auth.username = "node";
opts.auth.password = "secret";
opts.tls.caFile    = "/etc/ssl/certs/ca-certificates.crt";
opts.tls.verifyPeer = true;
opts.will.enabled  = true;
opts.will.topic    = "nodes/node-01/status";
opts.will.payload  = "offline";
opts.will.qos      = 1;
opts.will.retain   = true;
opts.defaults.publishQos = 1;

auto conn = std::make_shared<MqttConnection>(
    "wss://broker.example.com:8884/mqtt", "node-01", opts);
conn->connect();
```

| URI scheme | Transport |
|---|---|
| `mqtt://host:1883` | Plain TCP |
| `mqtts://host:8883` | TLS/TCP |
| `ws://host:9001/mqtt` | WebSocket |
| `wss://host:8884/mqtt` | TLS WebSocket |

---

### WebRTC Streaming

WebRTC enables **P2P communication over the internet** — no broker in the data path after the initial signaling handshake. Signaling is exchanged via MQTT (internet) or ZMQ (LAN).

> **Note:** The C++ implementation uses [libdatachannel](https://github.com/paullouisageneau/libdatachannel), which provides data channels only — there is no RTP/SRTP media track stack. All frames (including video and audio) are transported over WebRTC data channels. When interoperating with a Python peer that has `use_media_channels=True`, set `useMediaChannels=false` on the C++ side so both peers agree to use the data-channel path.

**Writer:**

```cpp
#include <magpie/frames/primitive_frames.hpp>
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/webrtc_connection.hpp>
#include <magpie/transport/webrtc_stream_writer.hpp>

int main() {
    using namespace magpie;

    auto sig  = std::make_shared<MqttConnection>("mqtt://broker.hivemq.com:1883");
    sig->connect();

    auto conn = std::make_shared<WebRtcConnection>(sig, "my-robot");
    if (!conn->connect(30.0)) { return 1; }

    WebRtcStreamWriter writer(conn);

    DictFrame state;
    state["x"] = 1.0;
    writer.write(state, "robot/state");

    writer.close();
    conn->disconnect();
    sig->disconnect();
}
```

**Reader:**

```cpp
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/webrtc_connection.hpp>
#include <magpie/transport/webrtc_stream_reader.hpp>

int main() {
    using namespace magpie;

    auto sig  = std::make_shared<MqttConnection>("mqtt://broker.hivemq.com:1883");
    sig->connect();

    auto conn = std::make_shared<WebRtcConnection>(sig, "my-robot");
    if (!conn->connect(30.0)) { return 1; }

    WebRtcStreamReader reader(conn, "robot/state");
    while (true) {
        std::unique_ptr<Frame> frame;
        std::string topic;
        if (reader.read(frame, topic, /*timeoutSec=*/3.0))
            Logger::info("data: " + topic);
    }

    reader.close();
    conn->disconnect();
    sig->disconnect();
}
```

---

### WebRTC Request / Response RPC

No broker in the hot path — the data channel is bidirectional P2P.

**Responder:**

```cpp
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/webrtc_connection.hpp>
#include <magpie/transport/webrtc_rpc_responder.hpp>

int main() {
    using namespace magpie;

    auto sig  = std::make_shared<MqttConnection>("mqtt://broker.hivemq.com:1883");
    sig->connect();
    auto conn = std::make_shared<WebRtcConnection>(sig, "my-node-rpc");
    if (!conn->connect(30.0)) { return 1; }

    WebRtcRpcResponder server(conn, "service/actions");
    while (true) {
        server.handleOnce([](const Value& req) -> Value {
            return Value::fromString("ok");
        }, /*timeoutSec=*/1.0);
    }

    conn->disconnect();
    sig->disconnect();
}
```

**Requester:**

```cpp
#include <magpie/transport/mqtt_connection.hpp>
#include <magpie/transport/webrtc_connection.hpp>
#include <magpie/transport/webrtc_rpc_requester.hpp>
#include <magpie/transport/timeout_error.hpp>

int main() {
    using namespace magpie;

    auto sig  = std::make_shared<MqttConnection>("mqtt://broker.hivemq.com:1883");
    sig->connect();
    auto conn = std::make_shared<WebRtcConnection>(sig, "my-node-rpc");
    if (!conn->connect(30.0)) { return 1; }

    WebRtcRpcRequester client(conn, "service/actions");
    try {
        Value::Dict d;
        d["action"] = Value::fromString("move");
        Value response = client.call(Value::fromDict(d), /*timeoutSec=*/5.0);
        Logger::info("response: " + response.toDebugString());
    } catch (const TimeoutError& e) {
        Logger::error(std::string("timeout: ") + e.what());
    }

    client.close();
    conn->disconnect();
    sig->disconnect();
}
```

---

### WebRTC Advanced Options

```cpp
#include <magpie/transport/webrtc_options.hpp>

WebRtcOptions opts;
opts.iceServers = {};                                                // disable STUN (LAN use)
opts.iceServers.push_back({"turn:myturn.server:3478", "u", "p"});  // add TURN relay
opts.iceTransportPolicy = "relay";   // force relay only
opts.reconnect = true;               // auto-reconnect on disconnect
opts.useMediaChannels = false;       // route all frames over the reliable data channel

auto conn = std::make_shared<WebRtcConnection>(sig, "my-robot", opts);
```

---

### Schema-based RPC

`JsonRpcSchema` adds JSON-RPC 2.0 dispatch on top of any MAGPIE transport. Define your API once — shape, description, and types — then attach handlers and call methods by name. The same schema object works on both sides.

**Responder — two ways to define methods:**

```cpp
#include <magpie/transport/zmq_rpc_responder.hpp>
#include <magpie/schema/json_rpc_schema.hpp>

int main() {
    using namespace magpie;

    // Way A: load from JSON, attach handlers with set_handler()
    auto schema = JsonRpcSchema::from_json_string(R"([
        {
            "name": "add",
            "description": "Add two numbers",
            "inputSchema": {
                "type": "object",
                "properties": {"a": {"type": "number"}, "b": {"type": "number"}},
                "required": ["a", "b"]
            }
        }
    ])");

    schema->set_handler("add", [](const Value::Dict& p) -> Value {
        return Value::fromDouble(p.at("a").asDouble() + p.at("b").asDouble());
    });

    // Way B: register_method() with inline handler and description
    schema->register_method("mul",
        [](const Value::Dict& p) -> Value {
            return Value::fromDouble(p.at("a").asDouble() * p.at("b").asDouble());
        },
        "Multiply two numbers");

    ZmqRpcResponder server("tcp://*:5556", nullptr, true, schema);
    while (true)
        server.handleOnce(/*handler=*/nullptr, /*timeoutSec=*/1.0);
}
```

The JSON string or file uses standard MCP / JSON Schema format. `description` and `outputSchema` are optional:

```json
[
  {
    "name": "convert",
    "description": "Convert a value from one unit to another",
    "inputSchema": {
      "type": "object",
      "properties": {
        "value":     {"type": "number"},
        "from_unit": {"type": "string"},
        "to_unit":   {"type": "string"}
      },
      "required": ["value", "from_unit", "to_unit"]
    },
    "outputSchema": {
      "type": "object",
      "properties": {"result": {"type": "number"}, "unit": {"type": "string"}}
    }
  }
]
```

**Requester — schema-based `call()`:**

```cpp
#include <magpie/transport/zmq_rpc_requester.hpp>
#include <magpie/schema/json_rpc_schema.hpp>

int main() {
    using namespace magpie;

    auto schema = JsonRpcSchema::from_json_string(R"([
        {"name": "add", "description": "Add two numbers"},
        {"name": "mul", "description": "Multiply two numbers"}
    ])");

    ZmqRpcRequester client("tcp://127.0.0.1:5556", nullptr, {}, 2.0, schema);

    // Schema-based call — JSON-RPC wrap/unwrap handled automatically
    Value result = client.call("add", {{"a", Value::fromDouble(3)}, {"b", Value::fromDouble(4)}});
    Logger::info("add: " + result.toDebugString());   // 7.0

    try {
        client.call("nonexistent", {});
    } catch (const JsonRpcError& e) {
        Logger::warning(std::to_string(e.code()) + " " + e.message());  // -32601 Method not found
    }

    client.close();
}
```

---

### MCP Integration

MAGPIE C++ has native MCP server support — no separate MCP server process required.

`McpSchema` extends `JsonRpcSchema` with the full MCP handshake. Any registered method is automatically exposed as an MCP tool. A FastMCP `Client` using the Python `McpTransport` can call those tools over ZMQ, MQTT, or WebRTC without any port forwarding or VPN.

#### Server side — serve MCP tools over any transport

```cpp
#include <magpie/transport/zmq_rpc_responder.hpp>
#include <magpie/schema/mcp_schema.hpp>

int main() {
    using namespace magpie;

    auto schema = McpSchema::from_json_string(R"([
        {
            "name": "translate",
            "description": "Translate text into the target language",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "text":        {"type": "string"},
                    "target_lang": {"type": "string"}
                },
                "required": ["text", "target_lang"]
            }
        },
        {
            "name": "summarize",
            "description": "Summarize text to at most max_length characters",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "text":       {"type": "string"},
                    "max_length": {"type": "integer"}
                },
                "required": ["text", "max_length"]
            }
        }
    ])", "my-service", "1.0.0");

    schema->set_handler("translate", [](const Value::Dict& p) -> Value {
        const std::string lang = p.at("target_lang").asString();
        const std::string text = p.at("text").asString();
        return Value::fromString("[" + lang + "] " + text);
    });

    schema->set_handler("summarize", [](const Value::Dict& p) -> Value {
        const std::string text = p.at("text").asString();
        const int maxLen = static_cast<int>(p.at("max_length").asInt());
        return Value::fromString(text.substr(0, maxLen));
    });

    // ZMQ — no broker needed
    ZmqRpcResponder server("tcp://*:5556", nullptr, true, schema);
    while (true)
        server.handleOnce(/*handler=*/nullptr, /*timeoutSec=*/1.0);
}
```

Attach the same `schema` to any responder:

```cpp
// MQTT — service behind NAT
auto conn = std::make_shared<MqttConnection>("mqtt://broker.hivemq.com:1883");
conn->connect();
MqttRpcResponder server(conn, "node-01", nullptr, {}, -1, schema);

// WebRTC — P2P, lowest latency
auto conn = std::make_shared<WebRtcConnection>(sig, "node-01");
conn->connect(30.0);
WebRtcRpcResponder server(conn, "node-01", {}, schema);
```

Serve loop is the same for all:

```cpp
while (true)
    server.handleOnce(nullptr, 1.0);
```

#### Agent / cloud side — call tools with FastMCP Client

The Python `McpTransport` connects a FastMCP `Client` to any MAGPIE C++ MCP server:

```python
import asyncio
from fastmcp import Client
from fastmcp.exceptions import ToolError
from luxai.magpie.adapters.mcp import McpTransport
from luxai.magpie.transport import ZMQRpcRequester

async def main():
    req = ZMQRpcRequester("tcp://127.0.0.1:5556")

    async with Client(McpTransport(req)) as client:
        tools = await client.list_tools()
        for tool in tools:
            print(f"  {tool.name}: {tool.description}")

        result = await client.call_tool("translate", {"text": "Hello", "target_lang": "fr"})
        print(result.content[0].text)

        try:
            await client.call_tool("translate", {"text": "Hello"})   # missing target_lang
        except ToolError as e:
            print(f"tool error: {e}")

    req.close()

asyncio.run(main())
```

For MQTT or WebRTC, just swap the requester — `McpTransport` is identical:

```python
# MQTT
from luxai.magpie.transport.mqtt import MqttConnection
from luxai.magpie.transport import MqttRpcRequester

conn = MqttConnection("mqtt://broker.hivemq.com:1883")
conn.connect()
req = MqttRpcRequester(conn, service_name="node-01")

async with Client(McpTransport(req)) as client:
    result = await client.call_tool("translate", {"text": "Hello", "target_lang": "fr"})
```

#### Loading tools from a JSON file

```cpp
auto schema = McpSchema::from_json_file("tools.json", "my-service", "1.0.0");

schema->set_handler("translate", [](const Value::Dict& p) -> Value {
    return Value::fromString("[" + p.at("target_lang").asString() + "] " + p.at("text").asString());
});
```

---

### Network Discovery

**Advertise a node:**

```cpp
#include <magpie/discovery/zconf_discovery.hpp>
#include <magpie/utils/logger.hpp>

#include <chrono>
#include <thread>

int main() {
    using namespace magpie;

    ZconfDiscovery disc;
    disc.start();
    disc.advertise("my-node", 5555, "zmq", R"({"role":"service"})");
    Logger::info("advertising my-node on port 5555");

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

#include <chrono>
#include <thread>

int main() {
    using namespace magpie;

    ZconfDiscovery disc;
    disc.start();

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        for (const auto& info : disc.listNodes()) {
            const std::string ip = ZconfDiscovery::pickBestIp(info.ips);
            Logger::info("node=" + info.nodeId +
                         " tcp://" + ip + ":" + std::to_string(info.port) +
                         " payload=" + info.payload);
        }
    }

    disc.close();
}
```

---

## Architecture

MAGPIE C++ is built around four abstract base classes — `StreamWriter`, `StreamReader`, `RpcRequester`, `RpcResponder` — that absorb all threading, queuing, and lifecycle complexity. Transport implementations fill in two or three pure transport methods; everything else is handled by the base classes. This makes adding a new transport a matter of minutes, not days, and keeps user code completely transport-agnostic.

| Component | Description |
|---|---|
| `ZmqStream{Writer,Reader}` / `ZmqRpc{Requester,Responder}` | ZeroMQ transport (core) |
| `MqttStream{Writer,Reader}` / `MqttRpc{Requester,Responder}` | MQTT transport (`magpie::mqtt`) |
| `WebRtcStream{Writer,Reader}` / `WebRtcRpc{Requester,Responder}` | WebRTC transport (`magpie::webrtc`) |
| `JsonRpcSchema` | JSON-RPC 2.0 method dispatch + envelope wrap/unwrap (core) |
| `McpSchema` | MCP protocol server — extends `JsonRpcSchema`, adds `initialize` / `tools/list` / `tools/call` (core) |
| `BaseNode`, `SourceNode`, `SinkNode`, `ServerNode`, `ProcessNode` | Lifecycle and threading helpers (core) |
| `ZconfDiscovery` | mDNS/Zeroconf node advertisement and scanning (core) |

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
