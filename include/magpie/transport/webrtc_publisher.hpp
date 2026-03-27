#pragma once

#include <memory>
#include <string>

#include <magpie/frames/frame.hpp>
#include <magpie/serializer/serializer.hpp>
#include <magpie/transport/webrtc_connection.hpp>
#include <magpie/transport/stream_writer.hpp>

namespace magpie {

/**
 * WebRtcPublisher
 *
 * Publishes Frame objects to the remote peer over the shared WebRTC data
 * channel.  Extends StreamWriter so frames can be queued and sent from a
 * background thread.
 *
 * @code
 * auto conn = std::make_shared<WebRtcConnection>(signalConn, "my-robot");
 * conn->connect(30.0);
 *
 * WebRtcPublisher pub(conn);
 * DictFrame f({{"sensor", "temp"}, {"value", 22.5}});
 * pub.write(f, "sensors/temperature");
 *
 * pub.close();
 * conn->disconnect();
 * @endcode
 */
class WebRtcPublisher : public StreamWriter {
public:
    /**
     * @param connection  Shared, already-connected WebRtcConnection.
     * @param serializer  Serializer for frame encoding (defaults to MsgpackSerializer).
     * @param queueSize   StreamWriter queue depth (0 = synchronous write).
     */
    explicit WebRtcPublisher(std::shared_ptr<WebRtcConnection> connection,
                              std::shared_ptr<Serializer>       serializer = nullptr,
                              int                               queueSize  = 10);

    ~WebRtcPublisher() override;

    WebRtcPublisher(const WebRtcPublisher&)            = delete;
    WebRtcPublisher& operator=(const WebRtcPublisher&) = delete;

protected:
    void transportWrite(const Frame& frame, const std::string& topic) override;
    void transportClose() override;

private:
    std::shared_ptr<WebRtcConnection> connection_;
    std::shared_ptr<Serializer>       serializer_;
};

} // namespace magpie
