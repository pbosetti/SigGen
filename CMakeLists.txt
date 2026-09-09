cmake_minimum_required(VERSION 3.20)
project(SigGen VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# --- Dependency resolution ------------------------------------------------
# A project consuming SigGen may well use these same libraries itself, and must
# not end up with two copies of any of them.
#
# FetchContent covers half of that on its own: it honours the first declaration
# of a name, so a consumer that declares nlohmann_json before pulling SigGen in
# keeps its own pin, and only one copy is ever downloaded.
#
# It does not cover a consumer that obtained the library some other way. After
# a find_package() or an add_subdirectory() there is a target but no
# declaration for FetchContent to defer to, so an unguarded fetch here would
# add a second copy of the sources and then fail outright -- nlohmann/json, for
# one, defines nlohmann_json::nlohmann_json unconditionally, which collides
# with the imported target find_package() has just created.
#
# siggen_dependency() closes that gap. Where the target already exists it
# adopts it and records the name as populated, so that a nested project
# declaring the same dependency -- Expressionist declares nlohmann_json too --
# leaves it alone as well. That is the same bookkeeping FetchContent itself
# performs when find_package() satisfies a declared dependency.
#
# A macro rather than a function, so that FetchContent's own variables land in
# this scope exactly as they would had the calls been written out inline.
macro(siggen_dependency name target)
  if(TARGET ${target})
    message(STATUS "SigGen: using ${target} already provided by the enclosing project")
    if(COMMAND FetchContent_SetPopulated)
      FetchContent_SetPopulated(${name})
    else()
      # FetchContent_SetPopulated() arrived in CMake 3.24; older versions read
      # this global property directly.
      string(TOLOWER "${name}" _siggen_name_lower)
      set_property(GLOBAL PROPERTY
        _FetchContent_${_siggen_name_lower}_populated TRUE)
      unset(_siggen_name_lower)
    endif()
  else()
    message(STATUS "SigGen: providing ${name} via FetchContent")
    FetchContent_Declare(${name} ${ARGN})
    FetchContent_MakeAvailable(${name})
  endif()
endmacro()

# --- Dependency: nlohmann/json --------------------------------------------
# Resolved before Expressionist on purpose: the sibling library declares this
# same dependency, and settling it here means it sees whatever this project --
# or this project's consumer -- has already decided on.
siggen_dependency(nlohmann_json nlohmann_json::nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# --- Dependency: Expressionist --------------------------------------------
# Evaluates algebraic expressions embedded in the JSON configuration.
siggen_dependency(Expressionist Expressionist::Expressionist
  GIT_REPOSITORY https://github.com/pbosetti/Expressionist.git
  GIT_TAG main
  GIT_SHALLOW TRUE
)

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

# --- Tools, tests and examples --------------------------------------------
# On by default for a top-level build and off for a subproject, so that a
# consumer gets nothing but the interface library unless it asks -- but it can
# ask, which is what makes the cxxopts and doctest reuse below reachable from
# downstream.
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  set(SIGGEN_EXTRAS_DEFAULT ON)
else()
  set(SIGGEN_EXTRAS_DEFAULT OFF)
endif()

option(SIGGEN_BUILD_TESTS "Build the SigGen test suite" ${SIGGEN_EXTRAS_DEFAULT})
option(SIGGEN_BUILD_TOOL "Build the siggen command-line tool" ${SIGGEN_EXTRAS_DEFAULT})
option(SIGGEN_BUILD_EXAMPLES "Build the SigGen examples" ${SIGGEN_EXTRAS_DEFAULT})
unset(SIGGEN_EXTRAS_DEFAULT)

if(SIGGEN_BUILD_EXAMPLES)
  add_executable(siggen_example example/main.cpp)
  target_link_libraries(siggen_example PRIVATE siggen)
endif()

if(SIGGEN_BUILD_TOOL)
  # cxxopts for command-line parsing.
  set(CXXOPTS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(CXXOPTS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(CXXOPTS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  # cxxopts 3.2.0 ships an old cmake_minimum_required that CMake 4.x rejects.
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
  siggen_dependency(cxxopts cxxopts::cxxopts
    GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
    GIT_TAG v3.2.0
    GIT_SHALLOW TRUE
  )
  unset(CMAKE_POLICY_VERSION_MINIMUM)

  add_executable(siggen_cli cli/main.cpp)
  set_target_properties(siggen_cli PROPERTIES OUTPUT_NAME siggen)
  target_link_libraries(siggen_cli PRIVATE siggen cxxopts::cxxopts)
  target_compile_definitions(siggen_cli PRIVATE
    SIGGEN_VERSION="${PROJECT_VERSION}")
endif()

# --- Optional C ABI and Python wrapper ------------------------------------
# Both are off by default even for a top-level build: they add the project's
# only compiled artefact, and the C++ library itself stays header-only.
option(SIGGEN_BUILD_C_API
  "Build the siggen_c shared library (plain C ABI)" OFF)
option(SIGGEN_BUILD_PYTHON
  "Build and install the Python wrapper (implies SIGGEN_BUILD_C_API)" OFF)

if(SIGGEN_BUILD_PYTHON)
  set(SIGGEN_BUILD_C_API ON)
endif()

if(SIGGEN_BUILD_C_API)
  include(GNUInstallDirs)

  add_library(siggen_c SHARED src/siggen_c.cpp)
  add_library(SigGen::SigGen_C ALIAS siggen_c)
  target_include_directories(siggen_c PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  )
  target_link_libraries(siggen_c PRIVATE siggen)
  target_compile_definitions(siggen_c PRIVATE
    SIGGEN_C_BUILDING
    SIGGEN_VERSION="${PROJECT_VERSION}")
  # Nothing but the extern "C" entry points should be visible: the C++ library
  # is header-only, so without this every inlined template in it would be
  # exported from the shared object as well.
  set_target_properties(siggen_c PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

  install(TARGETS siggen_c
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(FILES include/siggen_c.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

if(SIGGEN_BUILD_PYTHON)
  # The interpreter alone resolves Python3_SITELIB, and it honours an active
  # virtualenv, so the wrapper lands wherever `python3` on PATH would import
  # from -- no PYTHONPATH or LD_LIBRARY_PATH setup for the user.
  find_package(Python3 COMPONENTS Interpreter REQUIRED)

  install(FILES python/siggen/__init__.py
    DESTINATION ${Python3_SITELIB}/siggen)
  install(TARGETS siggen_c
    LIBRARY DESTINATION ${Python3_SITELIB}/siggen
    RUNTIME DESTINATION ${Python3_SITELIB}/siggen)
endif()

if(SIGGEN_BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()
