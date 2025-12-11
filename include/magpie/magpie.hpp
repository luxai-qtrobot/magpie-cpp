#pragma once

// Umbrella header for magpie public API

#include <magpie/version.hpp>
#include <magpie/utils/logger.hpp>
#include <magpie/frames/frame.hpp>
#include <magpie/serializer/serializer.hpp>
#include <magpie/serializer/value.hpp>

#include <magpie/transport/timeout_error.hpp>
#include <magpie/transport/stream_reader.hpp>
#include <magpie/transport/stream_writer.hpp>
#include <magpie/transport/zmq_publisher.hpp>
#include <magpie/transport/zmq_subscriber.hpp>

// #include <magpie/transport/zmq_rpc_responder.hpp>
// #include <magpie/discovery/mcast_discovery.hpp>
// #include <magpie/nodes/base_node.hpp>
