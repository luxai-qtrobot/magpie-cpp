#include <magpie/nodes/base_node.hpp>
#include <magpie/utils/logger.hpp>

#include <chrono>
#include <thread>

struct MyNodeConfig { std::string message; };

class MyNode : public magpie::BaseNode {
public:
    MyNode(const MyNodeConfig& cfg, bool paused=false, std::string name="MyNode")
        : BaseNode(cfg, paused, std::move(name)) {}

protected:
    void setup() override {
        const auto& cfg = configAs<MyNodeConfig>();
        magpie::Logger::info(name() + " setup: " + cfg.message);
        message_ = cfg.message;
    }

    void process() override {
        magpie::Logger::info(name() + ": " + message_);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

private:
    std::string message_;
};

int main() {
    magpie::Logger::setLevel("DEBUG");

    MyNode node(MyNodeConfig{"Printing"});
    node.start();
    std::this_thread::sleep_for(std::chrono::seconds(6));
    node.terminate();
    return 0;
}

