// Minimal placeholder for Catch2 single-header.
// Replace this file with the real Catch2 v2 single-header release.
#pragma once

// Dummy testing macros to keep the skeleton compiling without real Catch2.
#include <iostream>

#define TEST_CASE(name, tags) void test_fn_##name()
#define REQUIRE(cond) do { if (!(cond)) { std::cerr << "Requirement failed: " #cond "\n"; } } while(0)
