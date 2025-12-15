cmake_minimum_required(VERSION 3.16)

find_package(RobotTestingFramework REQUIRED COMPONENTS DLL)

# include(CTest)
# enable_testing()

# Put all plugins in: <build>/rtf_tests/<config>
set(RTF_PLUGIN_DIR "${CMAKE_BINARY_DIR}/rtf_tests")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/rtf_tests")

# Collect plugin targets so we can make the runner depend on them
set(RTF_PLUGIN_TARGETS "")

function(add_rtf_plugin target)
  add_library(${target} MODULE ${ARGN})

  # 1) Link to your project lib (brings include dirs + symbols)
  target_link_libraries(${target} PRIVATE magpie::core)

  # 2) Link to RobotTestingFramework
  target_link_libraries(${target} PRIVATE
    RobotTestingFramework::RTF
    RobotTestingFramework::RTF_dll
  )

  # 3) Make sure your in-repo test macro header is findable
  # If you include it as: #include "external/rtf/macros.hpp"
  # then the include path must contain the repo root.
  target_include_directories(${target} PRIVATE
    ${CMAKE_SOURCE_DIR}
  )

  # 4) RTF plugins: avoid "lib" prefix and place outputs in rtf_tests/
  set_target_properties(${target} PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/rtf_tests"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/rtf_tests"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/rtf_tests"
    LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/rtf_tests"
    LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/rtf_tests"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/rtf_tests"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/rtf_tests"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/rtf_tests"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/rtf_tests"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/rtf_tests"
  )

  # 5) Remember this target for the global runner dependency
  set(RTF_PLUGIN_TARGETS ${RTF_PLUGIN_TARGETS} ${target} PARENT_SCOPE)
endfunction()

# ---- Add your plugins here (only one line per test) ----
add_rtf_plugin(TestDummy test_dummy.cpp)
add_rtf_plugin(TestValue test_value.cpp)
add_rtf_plugin(TestMsgpackSerializer test_msgpack_serializer.cpp)
add_rtf_plugin(TestFrame test_frame.cpp)

# ---- One testrunner invocation for all plugins (JUnit output) ----
set(RTF_JUNIT_XML "${CMAKE_BINARY_DIR}/result.xml")


list(APPEND CMAKE_PROGRAM_PATH "${CMAKE_PREFIX_PATH}/bin")
find_program(RTF_TESTRUNNER robottestingframework-testrunner REQUIRED)

add_test(
  NAME rtf_all_plugins
  COMMAND ${RTF_TESTRUNNER}
          -v
          --tests "${RTF_PLUGIN_DIR}"
          --recursive
          --output "${RTF_JUNIT_XML}"
          --output-type junit
)