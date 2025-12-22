cmake_minimum_required(VERSION 3.13)
project(magpie
    VERSION 0.6.0
    LANGUAGES CXX
)

option(MAGPIE_WITH_AUDIO      "Build audio-related components" OFF)
option(MAGPIE_WITH_VIDEO      "Build video-related components" OFF)
option(MAGPIE_BUILD_TESTS     "Build unit tests"               OFF)
option(MAGPIE_BUILD_EXAMPLES  "Build examples"                 ON)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

find_package(Threads REQUIRED)
find_package(msgpack REQUIRED)  # depending on your FindMsgpack or pkg-config setup
find_package(fmt REQUIRED)

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
    PRIVATE
        fmt::fmt    
        zmq      # or ZeroMQ::ZeroMQ, depending on your system
        msgpackc # or msgpackc-cxx / msgpack depending on your distro
)

target_compile_features(magpie_core PUBLIC cxx_std_14)

# Versioning for shared lib (Debian-friendly)
set_target_properties(magpie_core PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME magpie
)

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
        VERSION   ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        OUTPUT_NAME magpie-audio
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
        VERSION   ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        OUTPUT_NAME magpie-video
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
# Components: core, audio, video, discovery
# --------------------------------------------------------------------------------------

# Core runtime library -> libmagpie
install(TARGETS magpie_core
    EXPORT MagpieTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT core
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT core
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT core
)

# Optional runtime libs -> their own components
if(TARGET magpie_audio)
    install(TARGETS magpie_audio
        EXPORT MagpieTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT audio
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT audio
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT audio
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

if(TARGET magpie_discovery)
    install(TARGETS magpie_discovery
        EXPORT MagpieTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT discovery
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT discovery
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT discovery
    )
endif()

# Headers + CMake package files go with core package (minimal change).
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT core)

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
    COMPONENT core
)

install(EXPORT MagpieTargets
    FILE MagpieTargets.cmake
    NAMESPACE magpie::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Magpie
    COMPONENT core
)

# --------------------------------------------------------------------------------------
# CPack (DEB) - produces multiple .deb files via components
# --------------------------------------------------------------------------------------
set(CPACK_GENERATOR "DEB")
set(CPACK_DEB_COMPONENT_INSTALL ON)

# Base package metadata
set(CPACK_PACKAGE_NAME "magpie")
set(CPACK_PACKAGE_VENDOR "magpie")
set(CPACK_PACKAGE_CONTACT "maintainer@magpie.local")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "magpie C++ library")

# Install prefix inside packages (typical Debian)
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

# Components list (only build the packages that exist)
set(_magpie_components "core")
if(TARGET magpie_audio)
    list(APPEND _magpie_components "audio")
endif()
if(TARGET magpie_video)
    list(APPEND _magpie_components "video")
endif()
if(TARGET magpie_discovery)
    list(APPEND _magpie_components "discovery")
endif()
set(CPACK_COMPONENTS_ALL ${_magpie_components})

# Package names per component (the .deb filenames)
set(CPACK_DEBIAN_CORE_PACKAGE_NAME       "libmagpie")
set(CPACK_DEBIAN_AUDIO_PACKAGE_NAME      "libmagpie-audio")
set(CPACK_DEBIAN_VIDEO_PACKAGE_NAME      "libmagpie-video")
set(CPACK_DEBIAN_DISCOVERY_PACKAGE_NAME  "libmagpie-discovery")

# Let dpkg-shlibdeps compute runtime deps automatically (recommended)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_CORE_PACKAGE_SHLIBDEPS OFF)
set(CPACK_DEBIAN_AUDIO_PACKAGE_SHLIBDEPS OFF)
set(CPACK_DEBIAN_VIDEO_PACKAGE_SHLIBDEPS OFF)
set(CPACK_DEBIAN_DISCOVERY_PACKAGE_SHLIBDEPS OFF)

# Ensure optional packages pull in the core package
# (Version pin is conservative; adjust if you prefer looser deps.)
set(CPACK_DEBIAN_AUDIO_PACKAGE_DEPENDS     "libmagpie (= ${PROJECT_VERSION})")
set(CPACK_DEBIAN_VIDEO_PACKAGE_DEPENDS     "libmagpie (= ${PROJECT_VERSION})")
set(CPACK_DEBIAN_DISCOVERY_PACKAGE_DEPENDS "libmagpie (= ${PROJECT_VERSION})")

include(CPack)
