cmake_minimum_required(VERSION 3.16)

find_package(RobotTestingFramework REQUIRED COMPONENTS DLL)

# Put all plugins in: <build>/rtf_tests/<config> (config folder appears on multi-config generators)
set(RTF_PLUGIN_DIR "${CMAKE_BINARY_DIR}/rtf_tests/$<CONFIG>")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/rtf_tests") # base folder

# Helper to add one RTF plugin (one test per shared library)
function(add_rtf_plugin target)
  add_library(${target} MODULE ${ARGN})

  target_link_libraries(${target} PRIVATE
    RobotTestingFramework::RTF
    RobotTestingFramework::RTF_dll
  )

  # RTF plugins are typically loaded by filename; avoid "lib" prefix on Unix.
  set_target_properties(${target} PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/rtf_tests"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/rtf_tests/Debug"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/rtf_tests/Release"
    LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/rtf_tests/RelWithDebInfo"
    LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/rtf_tests/MinSizeRel"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/rtf_tests"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/rtf_tests/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/rtf_tests/Release"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/rtf_tests/RelWithDebInfo"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/rtf_tests/MinSizeRel"
  )
endfunction()

# ---- Add your plugins here (one .cpp per plugin/test) ----
# Example:
add_rtf_plugin(TestDummy test_dummy.cpp)
# add_rtf_plugin(TestLogger TestLogger.cpp)
# add_rtf_plugin(TestFoo    TestFoo.cpp)



# ---- One testrunner invocation for all plugins ----
include(CTest)
enable_testing()

# Where to write junit xml
set(RTF_JUNIT_XML "${CMAKE_BINARY_DIR}/result.xml")

add_test(
  NAME rtf_all_plugins
  COMMAND robottestingframework-testrunner
          -v
          --tests "${RTF_PLUGIN_DIR}"
          --recursive
          --output "${RTF_JUNIT_XML}"
          --output-type junit
)

# Make sure plugins are built before running the test
set_tests_properties(rtf_all_plugins PROPERTIES
  DEPENDS "TestDummy;MyTest"
)
