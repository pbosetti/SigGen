# doctest via FetchContent
FetchContent_Declare(
  doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG v2.4.11
  GIT_SHALLOW TRUE
)
# doctest 2.4.11 ships a very old cmake_minimum_required that CMake 4.x rejects;
# allow its build to configure under the modern CMake policy set.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(doctest)
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
)
target_link_libraries(siggen_tests PRIVATE siggen doctest::doctest)

if(MSVC)
  # doctest 2.4.11 declares a std::tuple specialization that the latest MSVC
  # flags as C5285 (forbidden by N5014) under C++20. It is benign and lives
  # entirely inside the dependency's header, so silence just that code here.
  target_compile_options(siggen_tests PRIVATE /wd5285)
endif()

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(siggen_tests)
