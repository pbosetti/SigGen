cmake_minimum_required(VERSION 3.20)
project(SigGen VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# --- Dependency: nlohmann/json -------------------------------------------
# Declared before Expressionist on purpose. FetchContent honours the first
# declaration of a name and Expressionist declares this same dependency, so
# pinning it here keeps the version under this project's control instead of
# inheriting whatever the sibling library happens to ask for.
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# --- Dependency: Expressionist -------------------------------------------
# Evaluates algebraic expressions embedded in the JSON configuration.
FetchContent_Declare(
  Expressionist
  GIT_REPOSITORY https://github.com/pbosetti/Expressionist.git
  GIT_TAG main
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Expressionist)

# --- The header-only library ---------------------------------------------
add_library(siggen INTERFACE)
add_library(SigGen::SigGen ALIAS siggen)

target_include_directories(siggen INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(siggen INTERFACE cxx_std_20)
target_link_libraries(siggen INTERFACE
  nlohmann_json::nlohmann_json
  Expressionist::Expressionist
)

# --- Tools, tests and examples (only when built as the top-level project) --
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  option(SIGGEN_BUILD_TESTS "Build the SigGen test suite" ON)
  option(SIGGEN_BUILD_TOOL "Build the siggen command-line tool" ON)
  option(SIGGEN_BUILD_EXAMPLES "Build the SigGen examples" ON)

  if(SIGGEN_BUILD_EXAMPLES)
    add_executable(siggen_example example/main.cpp)
    target_link_libraries(siggen_example PRIVATE siggen)
  endif()

  if(SIGGEN_BUILD_TOOL)
    # cxxopts for command-line parsing.
    set(CXXOPTS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CXXOPTS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(CXXOPTS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
      cxxopts
      GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
      GIT_TAG v3.2.0
      GIT_SHALLOW TRUE
    )
    # cxxopts 3.2.0 ships an old cmake_minimum_required that CMake 4.x rejects.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(cxxopts)
    unset(CMAKE_POLICY_VERSION_MINIMUM)

    add_executable(siggen_cli cli/main.cpp)
    set_target_properties(siggen_cli PROPERTIES OUTPUT_NAME siggen)
    target_link_libraries(siggen_cli PRIVATE siggen cxxopts::cxxopts)
    target_compile_definitions(siggen_cli PRIVATE
      SIGGEN_VERSION="${PROJECT_VERSION}")
  endif()

  if(SIGGEN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
  endif()
endif()
