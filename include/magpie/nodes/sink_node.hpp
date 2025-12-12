#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <magpie/nodes/process_node.hpp>
#include <magpie/transport/stream_reader.hpp>

namespace magpie {

/**
 * @brief SinkNode class.
 *
 * A SinkNode is a processing node that has **only an input stream**.
 * It reads data from a StreamReader and does not write to any StreamWriter.
 *
 * This mirrors the Python SinkNode design: it is used as the last stage
 * of a pipeline, consuming frames/messages (logging, storing, forwarding
 * to external systems, etc.). :contentReference[oaicite:0]{index=0}
 *
 * Key points:
 * - streamWriter() is always nullptr.
 * - You must implement process() in a derived class.
 * - Call start() explicitly when you want the worker loop to begin.
 *
 * Typical process() implementation:
 * - Read a frame/topic from streamReader()
 * - Consume / handle it
 */
class SinkNode : public ProcessNode {
public:
    /**
     * @brief Construct a SinkNode.
     *
     * @param streamReader Input StreamReader (required).
     * @param config        User-defined config object (stored type-erased in BaseNode).
     * @param paused       Start paused if true.
     * @param name         Node name (used for logging/debugging).
     *
     * @throws std::invalid_argument if streamReader is null.
     */
    template <typename Config>
    explicit SinkNode(std::shared_ptr<StreamReader> streamReader,
                      const Config& config,
                      bool paused = false,
                      std::string name = "SinkNode")
        : ProcessNode(std::move(streamReader),
                      /*streamWriter=*/nullptr,
                      config,
                      paused,
                      std::move(name))
    {
        if (!ProcessNode::streamReader()) {
            throw std::invalid_argument("SinkNode: streamReader cannot be null");
        }
    }

    ~SinkNode() override = default;

    SinkNode(const SinkNode&)            = delete;
    SinkNode& operator=(const SinkNode&) = delete;
};

} // namespace magpie
