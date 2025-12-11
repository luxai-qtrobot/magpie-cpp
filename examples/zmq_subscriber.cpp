
#include <magpie/frames/primitive_frames.hpp>
#include <magpie/transport/zmq_subscriber.hpp>
#include <magpie/transport/timeout_error.hpp>
#include <magpie/utils/logger.hpp>

#include <chrono>
#include <memory>
#include <thread>

int main() {
    using namespace magpie;

    Logger::setLevel("DEBUG");

    // Subscribe to /mytopic on localhost
    ZmqSubscriber sub("tcp://127.0.0.1:5555",
                      "/mytopic",                      
                      /*queueSize=*/10,
                      /*bind=*/false,
                      "reliable");

    while (true) {
        std::unique_ptr<Frame> frame;
        std::string topic;

        try {
            bool ok = sub.read(frame, topic, /*timeoutSec=*/5.0);
            if (!ok) {
                Logger::info("Subscriber: no frame (read returned false)");
                continue;
            }
        } catch (const TimeoutError&) {
            Logger::info("Subscriber: timeout waiting for frame");
            continue;
        }

        if (!frame) {
            Logger::warning("Subscriber: got null frame");
            continue;
        }

        auto* tf = dynamic_cast<StringFrame*>(frame.get());
        if (!tf) {
            Logger::warning("Subscriber: frame is not TestFrame (name=" + frame->name() + ")");
            continue;
        }

        Logger::info("Subscriber: got TestFrame topic=" + topic + " value=" + tf->value());
    }

    return 0;
}
