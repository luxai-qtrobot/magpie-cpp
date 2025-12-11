
#include <magpie/frames/primitive_frames.hpp>
#include <magpie/transport/zmq_publisher.hpp>
#include <magpie/utils/logger.hpp>

#include <chrono>
#include <memory>
#include <thread>

int main() {
    using namespace magpie;

    Logger::setLevel("DEBUG");

    // Direct write (queueSize = 0)
    ZmqPublisher pub("tcp://*:5555",/*queueSize=*/10, /*bind=*/true, "reliable");

    while (true) {
        StringFrame frame("hello from C++");
        Logger::info("Publisher: sending frame... ");
        pub.write(frame, "/mytopic");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    Logger::info("Publisher: done");
    return 0;
}
