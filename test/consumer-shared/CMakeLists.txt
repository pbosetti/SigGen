# A downstream project that already provides SigGen's dependencies itself.
#
# Copyright 2026 Paolo Bosetti
# SPDX-License-Identifier: Apache-2.0
#
# The companion to test/consumer, which provides none of them. This one pins
# nlohmann_json to a deliberately different version from the one SigGen would
# choose and pulls in cxxopts as well, then asks for SigGen's command-line tool
# so that cxxopts is genuinely needed on both sides. SigGen must adopt both
# rather than fetch its own, leaving exactly one copy of each in the build.
#
# Point SIGGEN_SOURCE_DIR at a checkout to test it, or leave it unset to fetch
# SigGen from GitHub the way a real consumer would.

cmake_minimum_required(VERSION 3.20)
project(SigGenSharedConsumer LANGUAGES CXX)

include(FetchContent)

# Deliberately older than SigGen's own pin: whichever copy survives, its
# version tells us who won.
set(CONSUMER_JSON_VERSION 3.11.3)
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v${CONSUMER_JSON_VERSION}/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_Declare(
  cxxopts
  GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
  GIT_TAG v3.2.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json cxxopts)
unset(CMAKE_POLICY_VERSION_MINIMUM)

# Ask for the tool, so cxxopts is a dependency of SigGen here too.
set(SIGGEN_BUILD_TOOL ON CACHE BOOL "" FORCE)

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

# SigGen must not have replaced the dependencies we established above.
if(NOT TARGET nlohmann_json::nlohmann_json)
  message(FATAL_ERROR "nlohmann_json::nlohmann_json disappeared")
endif()
if(NOT TARGET cxxopts::cxxopts)
  message(FATAL_ERROR "cxxopts::cxxopts disappeared")
endif()
if(NOT EXISTS "${nlohmann_json_SOURCE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "nlohmann_json source directory is not where we left it")
endif()
file(READ "${nlohmann_json_SOURCE_DIR}/CMakeLists.txt" CONSUMER_JSON_CMAKELISTS)
if(NOT CONSUMER_JSON_CMAKELISTS MATCHES
   "project\\(nlohmann_json VERSION ${CONSUMER_JSON_VERSION}")
  message(FATAL_ERROR
    "SigGen replaced nlohmann_json ${CONSUMER_JSON_VERSION} with its own copy")
endif()
message(STATUS "consumer-shared: SigGen adopted nlohmann_json "
               "${CONSUMER_JSON_VERSION} and cxxopts")

add_executable(consumer_shared main.cpp)
target_link_libraries(consumer_shared PRIVATE
  SigGen::SigGen
  nlohmann_json::nlohmann_json
  cxxopts::cxxopts
)
