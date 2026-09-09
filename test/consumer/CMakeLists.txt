# A minimal downstream project, used to check that SigGen is consumable.
#
# Copyright 2026 Paolo Bosetti
# SPDX-License-Identifier: Apache-2.0
#
# Point SIGGEN_SOURCE_DIR at a checkout to test it, or leave it unset to fetch
# SigGen from GitHub the way a real consumer would.

cmake_minimum_required(VERSION 3.20)
project(SigGenConsumer LANGUAGES CXX)

include(FetchContent)

set(SIGGEN_SOURCE_DIR "" CACHE PATH "Local SigGen checkout to consume")
if(SIGGEN_SOURCE_DIR)
  FetchContent_Declare(SigGen SOURCE_DIR "${SIGGEN_SOURCE_DIR}")
else()
  FetchContent_Declare(
    SigGen
    GIT_REPOSITORY https://github.com/pbosetti/SigGen.git
    GIT_TAG main
  )
endif()
FetchContent_MakeAvailable(SigGen)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE SigGen::SigGen)
