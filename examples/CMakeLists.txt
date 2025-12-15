add_executable(example_utils utils.cpp)
target_link_libraries(example_utils PRIVATE magpie::core)

add_executable(example_zmq_publisher zmq_publisher.cpp)
target_link_libraries(example_zmq_publisher PRIVATE magpie::core)

add_executable(example_zmq_subscriber zmq_subscriber.cpp)
target_link_libraries(example_zmq_subscriber PRIVATE magpie::core)

add_executable(example_zmq_rpc_requester zmq_rpc_requester.cpp)
target_link_libraries(example_zmq_rpc_requester PRIVATE magpie::core)

add_executable(example_zmq_rpc_responder zmq_rpc_responder.cpp)
target_link_libraries(example_zmq_rpc_responder PRIVATE magpie::core)

add_executable(example_base_node base_node.cpp)
target_link_libraries(example_base_node PRIVATE magpie::core)

add_executable(example_server_node server_node.cpp)
target_link_libraries(example_server_node PRIVATE magpie::core)

if(MAGPIE_WITH_AUDIO)
    add_executable(example_audio_flac audio_flac.cpp)
    target_link_libraries(example_audio_flac PRIVATE magpie::core magpie::audio)
endif()

if(MAGPIE_WITH_VIDEO)
    add_executable(example_image_jpeg image_jpeg.cpp)
    target_link_libraries(example_image_jpeg PRIVATE magpie::core magpie::video)
endif()

if(MAGPIE_WITH_DISCOVERY)
    add_executable(example_zconf_discovery zconf_discovery.cpp)
    target_link_libraries(example_zconf_discovery PRIVATE magpie::core magpie::discovery)
endif()
