#include <magpie/utils/logger.hpp>
#include "external/catch2/catch.hpp"

TEST_CASE(Logger_basic, "[logger]") {
    magpie::Logger::info("test info");
    magpie::Logger::warning("test warn");
    magpie::Logger::error("test error");
    REQUIRE(true);
}
