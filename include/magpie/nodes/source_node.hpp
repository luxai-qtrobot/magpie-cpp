#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <magpie/nodes/process_node.hpp>
#include <magpie/transport/stream_writer.hpp>

namespace magpie {

/**
 * @brief SourceNode class.
 *
 * A SourceNode is a processing node that has **only an output stream**.
 * It writes data to a StreamWriter and does not read from any StreamReader.
 *
 * This mirrors the Python SourceNode design: it is used as the first stage
 * of a pipeline, producing frames/messages periodically or event-driven. :contentReference[oaicite:1]{index=1}
 *
 * Key points:
 * - streamReader() is always nullptr.
 * - You must implement process() in a derived class.
 * - Call start() explicitly when you want the worker loop to begin.
 *
 * Typical process() implementation:
 * - Produce/generate a frame/message
 * - Write it to streamWriter()
 */
class SourceNode : public ProcessNode {
public:
    /**
     * @brief Construct a SourceNode.
     *
     * @param streamWriter Output StreamWriter (required).
     * @param config        User-defined config object (stored type-erased in BaseNode).
     * @param paused       Start paused if true.
     * @param name         Node name (used for logging/debugging).
     *
     * @throws std::invalid_argument if streamWriter is null.
     */
    template <typename Config>
    explicit SourceNode(std::shared_ptr<StreamWriter> streamWriter,
                        const Config& config,
                        bool paused = false,
                        std::string name = "SourceNode")
        : ProcessNode(/*streamReader=*/nullptr,
                      std::move(streamWriter),
                      config,
                      paused,
                      std::move(name))
    {
        if (!ProcessNode::streamWriter()) {
            throw std::invalid_argument("SourceNode: streamWriter cannot be null");
        }
    }

    ~SourceNode() override = default;

    SourceNode(const SourceNode&)            = delete;
    SourceNode& operator=(const SourceNode&) = delete;
};

} // namespace magpie
