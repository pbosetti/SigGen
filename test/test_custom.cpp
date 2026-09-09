// Custom signals: interpolation, extrapolation and resampling.
//
// Copyright 2026 Paolo Bosetti
// SPDX-License-Identifier: Apache-2.0

#include "test_common.hpp"

#include <doctest/doctest.h>
#include <siggen/custom.hpp>

using namespace SigGen;

namespace {

std::vector<Custom::Point> ramp() {
  return {{0.0, 0.0}, {1.0, 10.0}, {2.0, 0.0}};
}

} // namespace

TEST_CASE("a custom signal is exact at its knots in every mode") {
  for (auto interp :
       {Custom::Interp::LINEAR, Custom::Interp::STEP, Custom::Interp::CUBIC}) {
    Custom custom(ramp(), interp, Custom::Extrap::CLAMP);
    CHECK(custom.value_at(0.0) == doctest::Approx(0.0));
    CHECK(custom.value_at(1.0) == doctest::Approx(10.0));
    CHECK(custom.value_at(2.0) == doctest::Approx(0.0));
  }
}

TEST_CASE("linear interpolation runs straight between knots") {
  Custom custom(ramp());
  CHECK(custom.value_at(0.25) == doctest::Approx(2.5));
  CHECK(custom.value_at(0.5) == doctest::Approx(5.0));
  CHECK(custom.value_at(1.5) == doctest::Approx(5.0));
}

TEST_CASE("step interpolation holds the preceding knot") {
  Custom custom(ramp(), Custom::Interp::STEP);
  CHECK(custom.value_at(0.01) == doctest::Approx(0.0));
  CHECK(custom.value_at(0.99) == doctest::Approx(0.0));
  CHECK(custom.value_at(1.01) == doctest::Approx(10.0));
}

TEST_CASE("cubic interpolation is smooth and stays near the data") {
  Custom custom(ramp(), Custom::Interp::CUBIC, Custom::Extrap::CLAMP);
  // The peak is at a knot, so the spline should not overshoot it much.
  CHECK(custom.value_at(0.5) > 4.0);
  CHECK(custom.value_at(0.5) < 8.0);
  // Symmetric data gives a symmetric spline.
  CHECK(custom.value_at(0.5) == doctest::Approx(custom.value_at(1.5)));
}

TEST_CASE("extrapolation modes behave as documented") {
  SUBCASE("clamp holds the end values") {
    Custom custom(ramp(), Custom::Interp::LINEAR, Custom::Extrap::CLAMP);
    CHECK(custom.value_at(-5.0) == doctest::Approx(0.0));
    CHECK(custom.value_at(99.0) == doctest::Approx(0.0));
  }
  SUBCASE("zero blanks everything outside the span") {
    Custom custom(ramp(), Custom::Interp::LINEAR, Custom::Extrap::ZERO);
    CHECK(custom.value_at(-0.5) == doctest::Approx(0.0));
    CHECK(custom.value_at(2.5) == doctest::Approx(0.0));
    CHECK(custom.value_at(1.0) == doctest::Approx(10.0));
  }
  SUBCASE("repeat loops the span, in both directions") {
    Custom custom(ramp(), Custom::Interp::LINEAR, Custom::Extrap::REPEAT);
    CHECK(custom.span() == doctest::Approx(2.0));
    CHECK(custom.value_at(2.5) == doctest::Approx(custom.value_at(0.5)));
    CHECK(custom.value_at(-1.5) == doctest::Approx(custom.value_at(0.5)));
    CHECK(custom.value_at(100.0 + 1.0) == doctest::Approx(10.0));
  }
}

TEST_CASE("the table is resampled at the signal's own rate") {
  Custom custom(ramp(), Custom::Interp::LINEAR, Custom::Extrap::CLAMP);
  custom.set_noiseless();
  custom.set_sample_rate(2.0);
  const auto values = custom.take(5);
  CHECK(values[0] == doctest::Approx(0.0));
  CHECK(values[1] == doctest::Approx(5.0));
  CHECK(values[2] == doctest::Approx(10.0));
  CHECK(values[3] == doctest::Approx(5.0));
  CHECK(values[4] == doctest::Approx(0.0));
}

TEST_CASE("points are sorted, so declaration order does not matter") {
  Custom shuffled({{2.0, 0.0}, {0.0, 0.0}, {1.0, 10.0}});
  CHECK(shuffled.start_time() == doctest::Approx(0.0));
  CHECK(shuffled.end_time() == doctest::Approx(2.0));
  CHECK(shuffled.value_at(0.5) == doctest::Approx(5.0));
}

TEST_CASE("a malformed table is rejected") {
  CHECK_THROWS_AS(Custom({}), SigGenException);
  CHECK_THROWS_AS(Custom({{1.0, 0.0}, {1.0, 5.0}}), SigGenException);
}

TEST_CASE("a single point gives a constant signal") {
  Custom constant({{0.0, 7.0}}, Custom::Interp::LINEAR, Custom::Extrap::CLAMP);
  constant.set_noiseless();
  CHECK(constant.value_at(-3.0) == doctest::Approx(7.0));
  CHECK(constant.value_at(3.0) == doctest::Approx(7.0));
  CHECK(constant.rms() == doctest::Approx(7.0));
}

TEST_CASE("rms integrates over time, not over knot count") {
  // A square pulse: value 1 for a tenth of the span, 0 elsewhere. Averaging
  // the knots would give 0.5; integrating gives sqrt(0.1).
  Custom pulse({{0.0, 1.0}, {0.1, 1.0}, {0.1 + 1e-12, 0.0}, {1.0, 0.0}},
               Custom::Interp::STEP, Custom::Extrap::REPEAT);
  CHECK(pulse.rms() == doctest::Approx(std::sqrt(0.1)).epsilon(0.01));
}
