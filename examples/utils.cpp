#include <magpie/utils/logger.hpp>
#include <magpie/utils/common.hpp>
#include <iostream>

int main() {
    using namespace magpie;

    Logger::setLevel("DEBUG");

    Logger::debug("debug message");
    Logger::info("info message");
    Logger::warning("warning message");
    Logger::error("error message");

    double ts = getUtcTimestamp();
    std::string id = getUniqueId();

    std::cout << "UTC timestamp: " << ts << "\n";
    std::cout << "Unique ID: " << id << "\n";

    return 0;
}
