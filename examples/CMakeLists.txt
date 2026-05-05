add_executable(example_utils utils.cpp)
target_link_libraries(example_utils PRIVATE magpie::core)

# ── ZMQ ───────────────────────────────────────────────────────────────────────

add_executable(example_zmq_stream_writer zmq/zmq_stream_writer.cpp)
target_link_libraries(example_zmq_stream_writer PRIVATE magpie::core)

add_executable(example_zmq_stream_reader zmq/zmq_stream_reader.cpp)
target_link_libraries(example_zmq_stream_reader PRIVATE magpie::core)

add_executable(example_zmq_rpc_requester zmq/zmq_rpc_requester.cpp)
target_link_libraries(example_zmq_rpc_requester PRIVATE magpie::core)

add_executable(example_zmq_rpc_responder zmq/zmq_rpc_responder.cpp)
target_link_libraries(example_zmq_rpc_responder PRIVATE magpie::core)

# ── Nodes ─────────────────────────────────────────────────────────────────────

add_executable(example_base_node nodes/base_node.cpp)
target_link_libraries(example_base_node PRIVATE magpie::core)

add_executable(example_server_node nodes/server_node.cpp)
target_link_libraries(example_server_node PRIVATE magpie::core)

# ── Discovery ─────────────────────────────────────────────────────────────────

add_executable(example_zconf_discovery discovery/zconf_discovery.cpp)
target_link_libraries(example_zconf_discovery PRIVATE magpie::core)

# ── Schema ────────────────────────────────────────────────────────────────────

add_executable(example_schema_zmq_responder schema/zmq_schema_responder.cpp)
target_link_libraries(example_schema_zmq_responder PRIVATE magpie::core)

add_executable(example_schema_zmq_requester schema/zmq_schema_requester.cpp)
target_link_libraries(example_schema_zmq_requester PRIVATE magpie::core)

# ── MCP ───────────────────────────────────────────────────────────────────────

add_executable(example_mcp_zmq_server mcp/zmq_mcp_server.cpp)
target_link_libraries(example_mcp_zmq_server PRIVATE magpie::core)

# ── Audio ─────────────────────────────────────────────────────────────────────

if(MAGPIE_WITH_AUDIO)
    add_executable(example_audio_flac audio_flac.cpp)
    target_link_libraries(example_audio_flac PRIVATE magpie::core magpie::audio)
endif()

# ── MQTT ──────────────────────────────────────────────────────────────────────

if(MAGPIE_WITH_MQTT)
    add_executable(example_mqtt_stream_writer mqtt/mqtt_stream_writer.cpp)
    target_link_libraries(example_mqtt_stream_writer PRIVATE magpie::core magpie::mqtt)

    add_executable(example_mqtt_stream_reader mqtt/mqtt_stream_reader.cpp)
    target_link_libraries(example_mqtt_stream_reader PRIVATE magpie::core magpie::mqtt)

    add_executable(example_mqtt_rpc_requester mqtt/mqtt_rpc_requester.cpp)
    target_link_libraries(example_mqtt_rpc_requester PRIVATE magpie::core magpie::mqtt)

    add_executable(example_mqtt_rpc_responder mqtt/mqtt_rpc_responder.cpp)
    target_link_libraries(example_mqtt_rpc_responder PRIVATE magpie::core magpie::mqtt)
endif()

# ── Video ─────────────────────────────────────────────────────────────────────

if(MAGPIE_WITH_VIDEO)
    add_executable(example_image_jpeg image_jpeg.cpp)
    target_link_libraries(example_image_jpeg PRIVATE magpie::core magpie::video)
endif()

# ── WebRTC ────────────────────────────────────────────────────────────────────

if(MAGPIE_WITH_WEBRTC)
    add_executable(example_webrtc_stream_writer webrtc/webrtc_stream_writer.cpp)
    target_link_libraries(example_webrtc_stream_writer PRIVATE magpie::core magpie::mqtt magpie::webrtc)

    add_executable(example_webrtc_stream_reader webrtc/webrtc_stream_reader.cpp)
    target_link_libraries(example_webrtc_stream_reader PRIVATE magpie::core magpie::mqtt magpie::webrtc)

    add_executable(example_webrtc_rpc_requester webrtc/webrtc_rpc_requester.cpp)
    target_link_libraries(example_webrtc_rpc_requester PRIVATE magpie::core magpie::mqtt magpie::webrtc)

    add_executable(example_webrtc_rpc_responder webrtc/webrtc_rpc_responder.cpp)
    target_link_libraries(example_webrtc_rpc_responder PRIVATE magpie::core magpie::mqtt magpie::webrtc)
endif()
