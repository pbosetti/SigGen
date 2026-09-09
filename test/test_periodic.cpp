// Periodic waveforms: analytic shape, amplitude and the default noise floor.
//
// Copyright 2026 Paolo Bosetti
// SPDX-License-Identifier: Apache-2.0

#include "test_common.hpp"

#include <doctest/doctest.h>
#include <siggen/periodic.hpp>

#include <cmath>
#include <numbers>

using namespace SigGen;

TEST_CASE("a sine reproduces its closed form exactly") {
  Sine sine(10.0, 2.0);
  sine.set_noiseless();
  sine.set_sample_rate(1000.0);
  const auto values = sine.take(300);
  for (std::size_t i = 0; i < values.size(); ++i) {
    const double expected = 2.0 * std::sin(2.0 * std::numbers::pi * 10.0 *
                                           static_cast<double>(i) / 1000.0);
    REQUIRE(values[i] == doctest::Approx(expected).epsilon(1e-12));
  }
}

TEST_CASE("a sine has the RMS its rms() advertises") {
  Sine sine(10.0, 3.0);
  sine.set_noiseless();
  sine.set_sample_rate(1000.0);
  CHECK(sine.rms() == doctest::Approx(3.0 / std::numbers::sqrt2));
  // A whole number of periods, so the discrete RMS matches the continuous one.
  CHECK(test::rms(sine.take(1000)) ==
        doctest::Approx(sine.rms()).epsilon(1e-9));
}

TEST_CASE("phase and offset shift the waveform") {
  Sine shifted(1.0, 1.0, std::numbers::pi / 2.0, 5.0);
  shifted.set_noiseless();
  shifted.set_sample_rate(4.0);
  const auto values = shifted.take(4);
  CHECK(values[0] == doctest::Approx(6.0));
  CHECK(values[1] == doctest::Approx(5.0));
  CHECK(values[2] == doctest::Approx(4.0));
  CHECK(values[3] == doctest::Approx(5.0));

  SUBCASE("the offset is excluded from rms(), and so from the noise floor") {
    CHECK(shifted.rms() == doctest::Approx(1.0 / std::numbers::sqrt2));
  }
}

TEST_CASE("a square wave honours its duty cycle") {
  Square square(1.0, 1.0, 0.3);
  square.set_noiseless();
  square.set_sample_rate(1000.0);
  const auto values = square.take(1000);
  std::size_t high = 0;
  for (double y : values) {
    CHECK((y == 1.0 || y == -1.0));
    if (y > 0.0)
      ++high;
  }
  CHECK(high == 300);
}

TEST_CASE("a square wave's RMS is its amplitude at any duty cycle") {
  for (double duty : {0.1, 0.5, 0.9}) {
    Square square(1.0, 2.5, duty);
    square.set_noiseless();
    square.set_sample_rate(1000.0);
    CHECK(square.rms() == doctest::Approx(2.5));
    CHECK(test::rms(square.take(1000)) == doctest::Approx(2.5));
  }
}

TEST_CASE("a square wave rejects a degenerate duty cycle") {
  CHECK_THROWS_AS(Square(1.0, 1.0, 0.0), SigGenException);
  CHECK_THROWS_AS(Square(1.0, 1.0, 1.0), SigGenException);
  CHECK_THROWS_AS(Square(1.0, 1.0, -0.2), SigGenException);
}

TEST_CASE("a triangle rises and falls between the peaks") {
  Triangle triangle(1.0, 1.0);
  triangle.set_noiseless();
  triangle.set_sample_rate(4.0);
  const auto values = triangle.take(4);
  CHECK(values[0] == doctest::Approx(-1.0));
  CHECK(values[1] == doctest::Approx(0.0));
  CHECK(values[2] == doctest::Approx(1.0));
  CHECK(values[3] == doctest::Approx(0.0));

  SUBCASE("its RMS is independent of the symmetry") {
    for (double symmetry : {0.2, 0.5, 0.8}) {
      Triangle asymmetric(1.0, 1.5, symmetry);
      asymmetric.set_noiseless();
      asymmetric.set_sample_rate(2000.0);
      CHECK(asymmetric.rms() == doctest::Approx(1.5 / std::numbers::sqrt3));
      CHECK(test::rms(asymmetric.take(2000)) ==
            doctest::Approx(asymmetric.rms()).epsilon(1e-3));
    }
  }
}

TEST_CASE("a triangle rejects a symmetry outside the unit interval") {
  CHECK_THROWS_AS(Triangle(1.0, 1.0, 1.5), SigGenException);
  CHECK_THROWS_AS(Triangle(1.0, 1.0, -0.1), SigGenException);
}

TEST_CASE("a sawtooth ramps across the full amplitude every period") {
  Sawtooth saw(1.0, 1.0);
  saw.set_noiseless();
  saw.set_sample_rate(4.0);
  const auto values = saw.take(5);
  CHECK(values[0] == doctest::Approx(-1.0));
  CHECK(values[1] == doctest::Approx(-0.5));
  CHECK(values[2] == doctest::Approx(0.0));
  CHECK(values[3] == doctest::Approx(0.5));
  // The wrap resets the ramp rather than continuing it.
  CHECK(values[4] == doctest::Approx(-1.0));
  CHECK(saw.rms() == doctest::Approx(1.0 / std::numbers::sqrt3));
}

TEST_CASE("a negative frequency runs the waveform backwards") {
  Sawtooth forward(1.0), backward(-1.0);
  forward.set_noiseless();
  backward.set_noiseless();
  forward.set_sample_rate(8.0);
  backward.set_sample_rate(8.0);
  const auto f = forward.take(4), b = backward.take(4);
  CHECK(b[1] == doctest::Approx(-f[1]));
  CHECK(b[2] == doctest::Approx(-f[2]));
}

TEST_CASE("every waveform carries a white-noise floor by default") {
  Sine sine(10.0, 1.0);
  REQUIRE(sine.snr_db().has_value());
  CHECK(*sine.snr_db() == doctest::Approx(default_snr_db));
  // 40 dB below an RMS of 1/sqrt(2).
  CHECK(sine.noise_sigma() ==
        doctest::Approx(1.0 / std::numbers::sqrt2 / 100.0));

  sine.set_sample_rate(1000.0);
  Sine clean(10.0, 1.0);
  clean.set_noiseless();
  clean.set_sample_rate(1000.0);
  CHECK(clean.noise_sigma() == doctest::Approx(0.0));

  const auto noisy = sine.take(20000);
  const auto exact = clean.take(20000);
  std::vector<double> residual;
  residual.reserve(noisy.size());
  for (std::size_t i = 0; i < noisy.size(); ++i)
    residual.push_back(noisy[i] - exact[i]);
  CHECK(test::stdev(residual) ==
        doctest::Approx(sine.noise_sigma()).epsilon(0.05));
}

TEST_CASE("the signal-to-noise ratio scales the noise with the amplitude") {
  Sine quiet(10.0, 1.0), loud(10.0, 100.0);
  CHECK(loud.noise_sigma() == doctest::Approx(100.0 * quiet.noise_sigma()));
  loud.set_snr_db(20.0);
  CHECK(loud.noise_sigma() == doctest::Approx(loud.rms() / 10.0));
}

TEST_CASE("a non-positive sample rate is rejected") {
  Sine sine;
  CHECK_THROWS_AS(sine.set_sample_rate(0.0), SigGenException);
  CHECK_THROWS_AS(sine.set_sample_rate(-100.0), SigGenException);
}

TEST_CASE("take_for converts a duration into a sample count") {
  Sine sine;
  sine.set_sample_rate(500.0);
  CHECK(sine.take_for(2.0).size() == 1000);
  CHECK_THROWS_AS(sine.take_for(-1.0), SigGenException);
}
