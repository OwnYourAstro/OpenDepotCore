# SPDX-FileCopyrightText: 2026 Sarah Rose
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20)

project(OpenDepotCore VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /W3 /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /DWIN32_LEAN_AND_MEAN)
endif()

option(OPENDEPOT_BUILD_TESTS "Build OpenDepotCore tests" ON)
option(OPENDEPOT_BUILD_C_API "Build the stable C ABI library" ON)
option(OPENDEPOT_BUILD_CLI "Build the CLI harness (Windows only)" ON)

configure_file(
    cmake/version_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/opendepot/version_config.h
    @ONLY
)

add_library(opendepot_core
    src/version.cpp
    src/adk.cpp
    src/flashcode_db.cpp
    src/model_db.cpp
    src/crypto/md5.cpp
    src/crypto/rc4.cpp
    src/crypto/sha256.cpp
    src/container/cfb.cpp
)
add_library(OpenDepot::Core ALIAS opendepot_core)

target_compile_features(opendepot_core PUBLIC cxx_std_17)
target_include_directories(opendepot_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(opendepot_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(WIN32)
    target_sources(opendepot_core PRIVATE src/radio/native_session.cpp)
endif()

if(OPENDEPOT_BUILD_C_API)
    add_library(opendepot_c
        src/capi/opendepot_c.cpp
    )
    add_library(OpenDepot::C ALIAS opendepot_c)
    target_link_libraries(opendepot_c PUBLIC opendepot_core)
    target_include_directories(opendepot_c PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    set_target_properties(opendepot_c PROPERTIES OUTPUT_NAME opendepot_c)
endif()

if(OPENDEPOT_BUILD_CLI)
    if(WIN32)
        add_executable(opendepot-cli
            tools/cli/main.cpp
        )
        target_link_libraries(opendepot-cli PRIVATE opendepot_core)
        set_target_properties(opendepot-cli PROPERTIES OUTPUT_NAME "opendepot-cli")
    else()
        message(STATUS
            "Skipping opendepot-cli: the harness still uses Win32 for process and serial I/O")
    endif()
endif()

if(OPENDEPOT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
