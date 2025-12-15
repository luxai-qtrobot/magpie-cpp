#include "external/rtf/macros.hpp"

RTF_TEST_CASE("TestDummy") {
    ROBOTTESTINGFRAMEWORK_TEST_REPORT("testing integers");
    ROBOTTESTINGFRAMEWORK_TEST_CHECK(2 < 3, "is not smaller");
}

