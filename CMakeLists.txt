cmake_minimum_required(VERSION 3.16)
project(magpie
    VERSION 0.6.3
    LANGUAGES CXX
)

option(MAGPIE_WITH_AUDIO      "Build audio-related components"  OFF)
option(MAGPIE_WITH_VIDEO      "Build video-related components"  OFF)
option(MAGPIE_WITH_MQTT       "Build MQTT transport components" OFF)
option(MAGPIE_WITH_WEBRTC     "Build WebRTC transport components (requires MAGPIE_WITH_MQTT and libdatachannel)" OFF)
option(MAGPIE_BUILD_TESTS     "Build unit tests"                OFF)
option(MAGPIE_BUILD_EXAMPLES  "Build examples"                  ON)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

# Suppress CMP0167 warning from msgpack-cxx's internal find_dependency(Boost)
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.29")
    cmake_policy(SET CMP0167 NEW)
endif()

find_package(Threads REQUIRED)
find_package(fmt REQUIRED)

# find_package(msgpack REQUIRED)  # depending on your FindMsgpack or pkg-config setup
find_package(msgpack-cxx CONFIG QUIET)
if(NOT msgpack-cxx_FOUND)
    find_package(msgpackc-cxx CONFIG QUIET)
endif()
if(NOT msgpack-cxx_FOUND AND NOT msgpackc-cxx_FOUND)
    find_package(msgpack CONFIG REQUIRED)  # fallback
endif()

# Pick the actual CMake target name exported by the found package.
set(MAGPIE_MSGPACK_TARGET "")

if(TARGET msgpack-cxx)
    set(MAGPIE_MSGPACK_TARGET msgpack-cxx)
elseif(TARGET msgpackc-cxx)
    set(MAGPIE_MSGPACK_TARGET msgpackc-cxx)
elseif(TARGET msgpackc)
    set(MAGPIE_MSGPACK_TARGET msgpackc)
elseif(TARGET msgpack)
    set(MAGPIE_MSGPACK_TARGET msgpack)
else()
    message(FATAL_ERROR
        "msgpack found, but no known CMake target exported (tried msgpack-cxx, msgpackc-cxx, msgpackc, msgpack)."
    )
endif()


set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# --------------------------------------------------------------------------------------
# Core library (always built)
# --------------------------------------------------------------------------------------
add_library(magpie_core SHARED
    src/utils/logger.cpp
    src/utils/common.cpp
    src/frames/frame.cpp
    src/frames/primitive_frames.cpp
    src/frames/audio_frame.cpp
    src/frames/image_frame.cpp
    src/serializer/value.cpp
    src/serializer/msgpack_serializer.cpp
    src/transport/stream_writer.cpp
    src/transport/stream_reader.cpp
    src/transport/rpc_requester.cpp
    src/transport/rpc_responder.cpp
    src/transport/zmq/zmq_publisher.cpp
    src/transport/zmq/zmq_subscriber.cpp
    src/transport/zmq/zmq_rpc_requester.cpp
    src/transport/zmq/zmq_rpc_responder.cpp
    src/discovery/zconf_discovery.cpp
)
add_library(magpie::core ALIAS magpie_core)

target_include_directories(magpie_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(magpie_core
    PUBLIC
        Threads::Threads
        fmt::fmt
    PRIVATE
        zmq      # or ZeroMQ::ZeroMQ, depending on your system
        ${MAGPIE_MSGPACK_TARGET}
)

target_compile_features(magpie_core PUBLIC cxx_std_14)

# Versioning for shared lib (Debian-friendly)
set_target_properties(magpie_core PROPERTIES
    VERSION     ${PROJECT_VERSION}
    SOVERSION   ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME magpie
    EXPORT_NAME core
)

# --------------------------------------------------------------------------------------
# Convenience target for consumers who DON'T want magpie::core
# --------------------------------------------------------------------------------------
add_library(magpie INTERFACE)
target_link_libraries(magpie INTERFACE magpie_core)
add_library(magpie::magpie ALIAS magpie)

# --------------------------------------------------------------------------------------
# Optional feature libraries (built as separate shared libs)
# --------------------------------------------------------------------------------------

# Audio
if(MAGPIE_WITH_AUDIO)
    add_library(magpie_audio SHARED
        src/frames/audio_frame_flac.cpp
    )
    add_library(magpie::audio ALIAS magpie_audio)

    target_link_libraries(magpie_audio
        PUBLIC
            magpie_core
        PRIVATE
            FLAC
    )

    target_compile_definitions(magpie_audio PUBLIC MAGPIE_WITH_AUDIO)
    target_compile_features(magpie_audio PUBLIC cxx_std_14)

    set_target_properties(magpie_audio PROPERTIES
        VERSION     ${PROJECT_VERSION}
        SOVERSION   ${PROJECT_VERSION_MAJOR}
        OUTPUT_NAME magpie-audio
        EXPORT_NAME audio
    )
endif()

# MQTT
if(MAGPIE_WITH_MQTT)
    # ---- Find Paho MQTT C++ library ----
    # Try CMake config first (installed by paho-mqtt-cpp >= 1.2)
    find_package(PahoMqttCpp CONFIG QUIET)

    if(PahoMqttCpp_FOUND)
        set(MAGPIE_PAHOMQTTCPP_TARGET PahoMqttCpp::paho-mqttpp3)
        message(STATUS "Found PahoMqttCpp via CMake config")
    else()
        # Manual fallback (e.g. libpaho-mqttpp3-dev on Ubuntu without cmake config)
        find_library(PAHOMQTTCPP_LIB    NAMES paho-mqttpp3 REQUIRED)
        find_path   (PAHOMQTTCPP_INCLUDE NAMES mqtt/async_client.h REQUIRED)

        add_library(PahoMqttCpp::paho-mqttpp3 UNKNOWN IMPORTED)
        set_target_properties(PahoMqttCpp::paho-mqttpp3 PROPERTIES
            IMPORTED_LOCATION        "${PAHOMQTTCPP_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${PAHOMQTTCPP_INCLUDE}"
        )
        set(MAGPIE_PAHOMQTTCPP_TARGET PahoMqttCpp::paho-mqttpp3)
        message(STATUS "Found paho-mqttpp3 via manual search: ${PAHOMQTTCPP_LIB}")
    endif()

    add_library(magpie_mqtt SHARED
        src/transport/mqtt/mqtt_connection.cpp
        src/transport/mqtt/mqtt_publisher.cpp
        src/transport/mqtt/mqtt_subscriber.cpp
        src/transport/mqtt/mqtt_rpc_requester.cpp
        src/transport/mqtt/mqtt_rpc_responder.cpp
    )
    add_library(magpie::mqtt ALIAS magpie_mqtt)

    target_link_libraries(magpie_mqtt
        PUBLIC
            magpie_core
        PRIVATE
            ${MAGPIE_PAHOMQTTCPP_TARGET}
    )

    target_compile_definitions(magpie_mqtt PUBLIC MAGPIE_WITH_MQTT)
    target_compile_features(magpie_mqtt PUBLIC cxx_std_17)

    set_target_properties(magpie_mqtt PROPERTIES
        VERSION     ${PROJECT_VERSION}
        SOVERSION   ${PROJECT_VERSION_MAJOR}
        OUTPUT_NAME magpie-mqtt
        EXPORT_NAME mqtt
    )
endif()

# Video
if(MAGPIE_WITH_VIDEO)
    add_library(magpie_video SHARED
        src/frames/image_frame_jpeg.cpp
    )
    add_library(magpie::video ALIAS magpie_video)

    find_library(TURBOJPEG_LIBRARY turbojpeg REQUIRED)

    target_link_libraries(magpie_video
        PUBLIC
            magpie_core
        PRIVATE
            ${TURBOJPEG_LIBRARY}
    )

    target_compile_definitions(magpie_video PUBLIC MAGPIE_WITH_VIDEO)
    target_compile_features(magpie_video PUBLIC cxx_std_14)

    set_target_properties(magpie_video PROPERTIES
        VERSION     ${PROJECT_VERSION}
        SOVERSION   ${PROJECT_VERSION_MAJOR}
        OUTPUT_NAME magpie-video
        EXPORT_NAME video
    )
endif()

# WebRTC
if(MAGPIE_WITH_WEBRTC)
    if(NOT MAGPIE_WITH_MQTT)
        message(FATAL_ERROR "MAGPIE_WITH_WEBRTC requires MAGPIE_WITH_MQTT=ON")
    endif()

    # ---- Find or fetch libdatachannel ----
    # Prefer a system-installed copy; if not found, build from source as static libs
    # so they get absorbed directly into libmagpie-webrtc.so (no extra .so to ship).
    find_package(LibDataChannel CONFIG QUIET)
    if(LibDataChannel_FOUND)
        if(TARGET LibDataChannel::LibDataChannel_static)
            set(MAGPIE_LIBDATACHANNEL_TARGET LibDataChannel::LibDataChannel_static)
        else()
            set(MAGPIE_LIBDATACHANNEL_TARGET LibDataChannel::LibDataChannel)
        endif()
        message(STATUS "Found libdatachannel via CMake config: ${MAGPIE_LIBDATACHANNEL_TARGET}")
    else()
        message(STATUS "libdatachannel not found on system -- fetching and building from source (static)")

        include(FetchContent)
        FetchContent_Declare(
            libdatachannel
            GIT_REPOSITORY    https://github.com/paullouisageneau/libdatachannel.git
            GIT_TAG           v0.21.2
            GIT_SUBMODULES_RECURSE ON   # requires CMake >= 3.17; silently ignored on 3.16
        )

        # Build libdatachannel and all its sub-dependencies (juice, usrsctp, libsrtp, …)
        # as static libraries so they get linked into our shared .so at build time.
        # We use a normal (non-cache) variable so it shadows any user-supplied cache
        # value only for the FetchContent subdirectory without permanently overriding it.
        set(BUILD_SHARED_LIBS OFF)
        set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
        set(NO_TESTS    ON CACHE BOOL "" FORCE)

        FetchContent_MakeAvailable(libdatachannel)

        # When built via FetchContent the static target is always 'datachannel-static'
        set(MAGPIE_LIBDATACHANNEL_TARGET datachannel-static)
    endif()

    # turbojpeg is used by the WebRTC layer for image encoding.
    # find_library caches its result, so this is a no-op if MAGPIE_WITH_VIDEO already
    # located the library in the same CMake run.
    find_library(TURBOJPEG_LIBRARY turbojpeg REQUIRED)

    add_library(magpie_webrtc SHARED
        src/transport/webrtc/webrtc_connection.cpp
        src/transport/webrtc/webrtc_publisher.cpp
        src/transport/webrtc/webrtc_subscriber.cpp
        src/transport/webrtc/webrtc_rpc_requester.cpp
        src/transport/webrtc/webrtc_rpc_responder.cpp
    )
    add_library(magpie::webrtc ALIAS magpie_webrtc)

    target_link_libraries(magpie_webrtc
        PUBLIC
            magpie_core
            magpie_mqtt
        PRIVATE
            ${MAGPIE_LIBDATACHANNEL_TARGET}
            ${TURBOJPEG_LIBRARY}
    )

    target_compile_definitions(magpie_webrtc PUBLIC MAGPIE_WITH_WEBRTC)
    target_compile_features(magpie_webrtc PUBLIC cxx_std_17)

    set_target_properties(magpie_webrtc PROPERTIES
        VERSION     ${PROJECT_VERSION}
        SOVERSION   ${PROJECT_VERSION_MAJOR}
        OUTPUT_NAME magpie-webrtc
        EXPORT_NAME webrtc
    )
endif()

# --------------------------------------------------------------------------------------
# Examples / Tests
# --------------------------------------------------------------------------------------
if(MAGPIE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(MAGPIE_BUILD_TESTS)
    set(BUILD_TESTING ON CACHE BOOL "Build tests" FORCE)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()

# --------------------------------------------------------------------------------------
# Installation (split into components for separate .deb packages)
# Components: core, audio, mqtt, video, discovery
# --------------------------------------------------------------------------------------

# Core runtime library -> libmagpie
install(TARGETS magpie_core
    EXPORT MagpieTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT core
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT core
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT core
)

# Export the convenience INTERFACE target too (no files installed, but included in exports)
install(TARGETS magpie
    EXPORT MagpieTargets
    COMPONENT core
)

if(TARGET magpie_audio)
    install(TARGETS magpie_audio
        EXPORT MagpieTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT audio
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT audio
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT audio
    )
endif()

if(TARGET magpie_mqtt)
    install(TARGETS magpie_mqtt
        EXPORT MagpieTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT mqtt
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT mqtt
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT mqtt
    )
endif()

if(TARGET magpie_video)
    install(TARGETS magpie_video
        EXPORT MagpieTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT video
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT video
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT video
    )
endif()

if(TARGET magpie_webrtc)
    install(TARGETS magpie_webrtc
        EXPORT MagpieTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT webrtc
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT webrtc
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT webrtc
    )
endif()

# Headers
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT core)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    cmake/magpieConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/magpieConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/magpie
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/magpieConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/magpieConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/magpieConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/magpie
    COMPONENT core
)

install(EXPORT MagpieTargets
    FILE MagpieTargets.cmake
    NAMESPACE magpie::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/magpie
    COMPONENT core
)

# --------------------------------------------------------------------------------------
# CPack (DEB) - produces multiple .deb files via components
# --------------------------------------------------------------------------------------
set(CPACK_GENERATOR "DEB")
set(CPACK_DEB_COMPONENT_INSTALL ON)

# Base package metadata
set(CPACK_PACKAGE_NAME "magpie")
set(CPACK_PACKAGE_VENDOR "LuxAI")
set(CPACK_PACKAGE_CONTACT "support@luxai.com")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "magpie C++ library")

# Install prefix inside packages (typical Debian)
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

# Architecture (Debian-compliant)
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
else()
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})
endif()

# Distro version (dynamic: e.g. 22.04 -> release suffix 1deb22.04)
execute_process(
    COMMAND lsb_release -rs
    OUTPUT_VARIABLE LINUX_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(LINUX_VERSION)
    set(CPACK_DEBIAN_PACKAGE_RELEASE "1deb${LINUX_VERSION}")
else()
    set(CPACK_DEBIAN_PACKAGE_RELEASE "1")
endif()

# Components list (only build the packages that exist)
set(_magpie_components "core")
if(TARGET magpie_audio)
    list(APPEND _magpie_components "audio")
endif()
if(TARGET magpie_mqtt)
    list(APPEND _magpie_components "mqtt")
endif()
if(TARGET magpie_video)
    list(APPEND _magpie_components "video")
endif()
if(TARGET magpie_discovery)
    list(APPEND _magpie_components "discovery")
endif()
if(TARGET magpie_webrtc)
    list(APPEND _magpie_components "webrtc")
endif()
set(CPACK_COMPONENTS_ALL ${_magpie_components})

# Use Debian-standard filename format: <package>_<version>-<release>_<arch>.deb
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

# Package names per component (the .deb filenames)
set(CPACK_DEBIAN_CORE_PACKAGE_NAME       "libmagpie")
set(CPACK_DEBIAN_AUDIO_PACKAGE_NAME      "libmagpie-audio")
set(CPACK_DEBIAN_MQTT_PACKAGE_NAME       "libmagpie-mqtt")
set(CPACK_DEBIAN_VIDEO_PACKAGE_NAME      "libmagpie-video")
set(CPACK_DEBIAN_DISCOVERY_PACKAGE_NAME  "libmagpie-discovery")
set(CPACK_DEBIAN_WEBRTC_PACKAGE_NAME     "libmagpie-webrtc")

# Let dpkg-shlibdeps compute runtime deps automatically for core only.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_CORE_PACKAGE_SHLIBDEPS      ON)

# Audio/video/mqtt: disable shlibdeps (libmagpie.so.0 is in a sibling component, not
# system-installed, so dpkg-shlibdeps cannot resolve it). Declare deps manually.
set(CPACK_DEBIAN_AUDIO_PACKAGE_SHLIBDEPS     OFF)
set(CPACK_DEBIAN_MQTT_PACKAGE_SHLIBDEPS      OFF)
set(CPACK_DEBIAN_VIDEO_PACKAGE_SHLIBDEPS     OFF)
set(CPACK_DEBIAN_DISCOVERY_PACKAGE_SHLIBDEPS OFF)

set(CPACK_DEBIAN_AUDIO_PACKAGE_DEPENDS     "libmagpie (>= ${PROJECT_VERSION})")
set(CPACK_DEBIAN_MQTT_PACKAGE_DEPENDS      "libmagpie (>= ${PROJECT_VERSION}), libpaho-mqttpp3-1 (>= 1.0)")
set(CPACK_DEBIAN_VIDEO_PACKAGE_DEPENDS     "libmagpie (>= ${PROJECT_VERSION})")
set(CPACK_DEBIAN_DISCOVERY_PACKAGE_DEPENDS "libmagpie (>= ${PROJECT_VERSION})")
set(CPACK_DEBIAN_WEBRTC_PACKAGE_SHLIBDEPS  OFF)
# libdatachannel is statically linked into this package, so no runtime dep on it.
set(CPACK_DEBIAN_WEBRTC_PACKAGE_DEPENDS    "libmagpie (>= ${PROJECT_VERSION}), libmagpie-mqtt (>= ${PROJECT_VERSION}), libturbojpeg0 (>= 1.0)")

include(CPack)
