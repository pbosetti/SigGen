# doctest, reusing the enclosing project's copy where there is one.
# Its own install rules would otherwise put doctest's headers and CMake config
# into our install prefix, which is not ours to fill -- nlohmann_json and
# cxxopts already keep theirs to themselves.
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
# doctest 2.4.11 ships a very old cmake_minimum_required that CMake 4.x
# rejects; allow its build to configure under the modern CMake policy set.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
siggen_dependency(doctest doctest::doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG v2.4.11
  GIT_SHALLOW TRUE
)
unset(CMAKE_POLICY_VERSION_MINIMUM)

add_executable(siggen_tests
  test_main.cpp
  test_periodic.cpp
  test_noise.cpp
  test_custom.cpp
  test_arima.cpp
  test_composite.cpp
  test_config.cpp
  test_determinism.cpp
  test_plot.cpp
  test_golden.cpp
)
target_link_libraries(siggen_tests PRIVATE siggen doctest::doctest)

if(MSVC)
  # doctest 2.4.11 declares a std::tuple specialization that the latest MSVC
  # flags as C5285 (forbidden by N5014) under C++20. It is benign and lives
  # entirely inside the dependency's header, so silence just that code here.
  target_compile_options(siggen_tests PRIVATE /wd5285)
endif()

# Register one ctest entry per test case. doctest_discover_tests() ships with
# doctest's sources, which are not on hand when the enclosing project supplied
# the library itself -- an installed copy exports the command instead, and a
# copy added some other way may expose neither. Fall back to running the binary
# as a single test, which still reports every failure, just less granularly.
if(NOT COMMAND doctest_discover_tests AND doctest_SOURCE_DIR)
  include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
endif()
if(COMMAND doctest_discover_tests)
  doctest_discover_tests(siggen_tests)
else()
  message(STATUS "SigGen: doctest_discover_tests() unavailable; "
                 "registering the suite as a single test")
  add_test(NAME siggen_tests COMMAND siggen_tests)
endif()

# --- C ABI tests ----------------------------------------------------------
# Compiled as C, not C++: the point is to prove that siggen_c.h is valid C and
# that the extern "C" entry points link, which a C++ test could not show.
if(SIGGEN_BUILD_C_API)
  enable_language(C)
  add_executable(siggen_c_smoke c_api_smoke.c)
  set_target_properties(siggen_c_smoke PROPERTIES C_STANDARD 17
                                                  C_STANDARD_REQUIRED ON)
  target_link_libraries(siggen_c_smoke PRIVATE siggen_c)
  add_test(NAME c_api_smoke COMMAND siggen_c_smoke)
endif()

# --- Python wrapper tests -------------------------------------------------
if(SIGGEN_BUILD_PYTHON)
  execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import pytest"
    RESULT_VARIABLE SIGGEN_PYTEST_MISSING
    OUTPUT_QUIET ERROR_QUIET)
  if(SIGGEN_PYTEST_MISSING EQUAL 0)
    add_test(NAME python_c_api_tests
      COMMAND ${Python3_EXECUTABLE} -m pytest ${CMAKE_CURRENT_SOURCE_DIR}/python -q)
    # Point the wrapper at the freshly built library rather than an installed
    # one, so the tests exercise the working tree.
    set_tests_properties(python_c_api_tests PROPERTIES
      ENVIRONMENT "SIGGEN_C_LIBRARY=$<TARGET_FILE:siggen_c>;PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/../python")
  else()
    message(STATUS "pytest not found for ${Python3_EXECUTABLE}; "
                   "skipping the Python wrapper tests")
  endif()
endif()
