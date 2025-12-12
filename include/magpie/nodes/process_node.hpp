#pragma once

#include <memory>
#include <string>
#include <utility>

#include <magpie/nodes/base_node.hpp>
#include <magpie/transport/stream_reader.hpp>
#include <magpie/transport/stream_writer.hpp>

namespace magpie {

/**
 * @brief ProcessNode class.
 *
 * A node in a processing pipeline that may read from an input stream and/or
 * write to an output stream.
 *
 * ProcessNode inherits from BaseNode and adds ownership of a StreamReader
 * and a StreamWriter. It is intended to be subclassed by concrete processing
 * nodes that implement process().
 *
 * Typical usage:
 *  - read a Frame (and optional topic) from the StreamReader
 *  - process or transform the data
 *  - write the result to the StreamWriter
 *
 * Configuration:
 *  - ProcessNode itself does not define a fixed config type.
 *  - Users may pass ANY typed config object through the constructor.
 *  - Derived classes retrieve it in setup() via configAs<T>().
 *
 * Termination behavior:
 *  - terminate() first closes the StreamWriter and StreamReader
 *  - then delegates to BaseNode::terminate()
 *  - interrupt() also closes streams to unblock any pending I/O
 */
class ProcessNode : public BaseNode {
public:
    /**
     * @brief Construct a ProcessNode with a typed user config.
     *
     * @tparam Config user-defined configuration type
     * @param streamReader  Input stream reader (may be nullptr for SourceNode-like behavior).
     * @param streamWriter  Output stream writer (may be nullptr for SinkNode-like behavior).
     * @param config        User-defined config object (stored type-erased in BaseNode).
     * @param paused        Start paused if true.
     * @param name          Node name for logging/debugging.
     */
    template <typename Config>
    ProcessNode(std::shared_ptr<StreamReader> streamReader,
                std::shared_ptr<StreamWriter> streamWriter,
                const Config& config,
                bool paused = false,
                std::string name = "ProcessNode")
        : BaseNode(config, paused, std::move(name))
        , streamReader_(std::move(streamReader))
        , streamWriter_(std::move(streamWriter))
    {}

    /**
     * @brief Construct a ProcessNode with no user config.
     *
     * This is useful when no configuration is needed.
     */
    ProcessNode(std::shared_ptr<StreamReader> streamReader,
                std::shared_ptr<StreamWriter> streamWriter,
                bool paused = false,
                std::string name = "ProcessNode")
        : BaseNode(EmptyConfig{}, paused, std::move(name))
        , streamReader_(std::move(streamReader))
        , streamWriter_(std::move(streamWriter))
    {}

    ~ProcessNode() override = default;

    ProcessNode(const ProcessNode&)            = delete;
    ProcessNode& operator=(const ProcessNode&) = delete;

    /**
     * @brief Get the input stream reader.
     */
    std::shared_ptr<StreamReader> streamReader() const { return streamReader_; }

    /**
     * @brief Get the output stream writer.
     */
    std::shared_ptr<StreamWriter> streamWriter() const { return streamWriter_; }

    /**
     * @brief Terminate the ProcessNode.
     *
     * Closes streams first to unblock pending I/O, then terminates the node loop.
     */
    void terminate(double timeoutSec = -1.0) {
        if (streamWriter_) streamWriter_->close();
        if (streamReader_) streamReader_->close();
        BaseNode::terminate(timeoutSec);
    }

protected:
    /**
     * @brief Interrupt hook.
     *
     * Called by BaseNode::terminate() to unblock I/O promptly.
     */
    void interrupt() override {
        if (streamWriter_) streamWriter_->close();
        if (streamReader_) streamReader_->close();
    }

    /**
     * @brief Main processing method.
     *
     * Must be implemented by derived classes.
     */
    void process() override = 0;

private:
    struct EmptyConfig {};

    std::shared_ptr<StreamReader> streamReader_;
    std::shared_ptr<StreamWriter> streamWriter_;
};

} // namespace magpie
