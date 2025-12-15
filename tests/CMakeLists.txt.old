add_library(Catch2 INTERFACE)
target_include_directories(Catch2 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/external/catch2)

add_executable(magpie_tests
    test_logger.cpp
    test_dummy.cpp
)
target_link_libraries(magpie_tests PRIVATE magpie::core Catch2)

add_test(NAME magpie_tests COMMAND magpie_tests)
