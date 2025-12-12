cmake_minimum_required(VERSION 3.13)
project(magpie
    VERSION 0.1.0
    LANGUAGES CXX
)

option(MAGPIE_WITH_AUDIO      "Build audio-related components" OFF)
option(MAGPIE_WITH_DISCOVERY  "Build discovery components"     OFF)
option(MAGPIE_BUILD_TESTS     "Build unit tests"               OFF)
option(MAGPIE_BUILD_EXAMPLES  "Build examples"                 ON)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

find_package(Threads REQUIRED)
find_package(msgpack REQUIRED)  # depending on your FindMsgpack or pkg-config setup
# find_package(PkgConfig REQUIRED)
# pkg_check_modules(ZMQ REQUIRED libzmq)


# Core library
add_library(magpie_core
    src/utils/logger.cpp
    src/utils/common.cpp
    src/frames/frame.cpp
    src/frames/primitive_frames.cpp
    src/serializer/msgpack_serializer.cpp
    src/transport/stream_writer.cpp
    src/transport/stream_reader.cpp
    src/transport/rpc_requester.cpp
    src/transport/rpc_responder.cpp
    src/transport/zmq/zmq_publisher.cpp
    src/transport/zmq/zmq_subscriber.cpp
)
add_library(magpie::core ALIAS magpie_core)

target_include_directories(magpie_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(magpie_core
    PUBLIC
        Threads::Threads
    PRIVATE        
        zmq          # or ZeroMQ::ZeroMQ, depending on your system
        msgpackc     # or msgpackc-cxx / msgpack depending on your distro
)

target_compile_features(magpie_core PUBLIC cxx_std_14)



# Optional: discovery (skeleton)
if(MAGPIE_WITH_DISCOVERY)
    add_library(magpie_discovery
        src/discovery/mcast_discovery.cpp
    )
    add_library(magpie::discovery ALIAS magpie_discovery)
    target_link_libraries(magpie_discovery PUBLIC magpie_core)
    target_include_directories(magpie_discovery
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )
endif()

# Optional: audio (placeholder)
if(MAGPIE_WITH_AUDIO)
    add_library(magpie_audio INTERFACE)
    add_library(magpie::audio ALIAS magpie_audio)
    target_link_libraries(magpie_audio INTERFACE magpie_core)
endif()

# Examples
if(MAGPIE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Tests
if(MAGPIE_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()

# Installation
install(TARGETS
    magpie_core
    EXPORT MagpieTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)


if(MAGPIE_WITH_DISCOVERY)
    install(TARGETS magpie_discovery
        EXPORT MagpieTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

include(CMakePackageConfigHelpers)
configure_package_config_file(
    cmake/MagpieConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/MagpieConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Magpie
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/MagpieConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/MagpieConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/MagpieConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Magpie
)

install(EXPORT MagpieTargets
    FILE MagpieTargets.cmake
    NAMESPACE magpie::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Magpie
)
