// Noise generators: amplitude calibration and spectral colour.
//
// Copyright 2026 Paolo Bosetti
// SPDX-License-Identifier: Apache-2.0

#include "test_common.hpp"

#include <doctest/doctest.h>
#include <siggen/noise.hpp>

using namespace SigGen;

TEST_CASE("noise generators carry no intrinsic noise of their own") {
  CHECK_FALSE(WhiteNoise().snr_db().has_value());
  CHECK_FALSE(PinkNoise().snr_db().has_value());
  CHECK_FALSE(BrownNoise().snr_db().has_value());
}

TEST_CASE("white noise matches its requested sigma") {
  for (double sigma : {0.25, 1.0, 4.0}) {
    WhiteNoise noise(sigma);
    CHECK(noise.rms() == doctest::Approx(sigma));
    const auto values = noise.take(100000);
    CHECK(test::stdev(values) == doctest::Approx(sigma).epsilon(0.02));
    CHECK(test::mean(values) ==
          doctest::Approx(0.0).scale(sigma).epsilon(0.05));
  }
}

TEST_CASE("white noise samples are uncorrelated") {
  WhiteNoise noise(1.0);
  CHECK(test::autocorrelation_1(noise.take(100000)) ==
        doctest::Approx(0.0).epsilon(1.0).scale(0.02));
}

TEST_CASE("pink noise matches its requested sigma") {
  // The closed-form normalisation is derived for the infinite process, so a
  // finite window under-counts the very lowest frequencies slightly.
  for (double sigma : {0.5, 1.0, 3.0}) {
    PinkNoise noise(sigma);
    CHECK(noise.rms() == doctest::Approx(sigma));
    CHECK(test::stdev(noise.take(200000)) ==
          doctest::Approx(sigma).epsilon(0.05));
  }
}

TEST_CASE("brown noise matches its requested sigma and stays bounded") {
  BrownNoise noise(1.0);
  CHECK(noise.rms() == doctest::Approx(1.0));
  const auto values = noise.take(200000);
  CHECK(test::stdev(values) == doctest::Approx(1.0).epsilon(0.05));

  // A leaky integrator, unlike a pure random walk, does not wander off.
  double peak = 0.0;
  for (double y : values)
    peak = std::max(peak, std::abs(y));
  CHECK(peak < 8.0);
}

TEST_CASE("brown noise rejects a leak outside the unit interval") {
  CHECK_THROWS_AS(BrownNoise(1.0, 1.0), SigGenException);
  CHECK_THROWS_AS(BrownNoise(1.0, -0.1), SigGenException);
}

TEST_CASE("the three colours are ordered by spectral slope") {
  // The lag-1 autocorrelation rises as the spectrum tilts towards low
  // frequencies: flat for white, mildly correlated for pink, and close to the
  // leak coefficient for brown.
  const double white = test::autocorrelation_1(WhiteNoise(1.0).take(100000));
  const double pink = test::autocorrelation_1(PinkNoise(1.0).take(100000));
  const double brown = test::autocorrelation_1(BrownNoise(1.0).take(100000));
  CHECK(white < pink);
  CHECK(pink < brown);
  CHECK(white == doctest::Approx(0.0).scale(0.02).epsilon(1.0));
  CHECK(brown == doctest::Approx(0.99).epsilon(0.02));
}

TEST_CASE("pink noise loses roughly 3 dB per octave") {
  // Compare the mean power of two adjacent octaves. A 1/f spectrum puts twice
  // the power per bin in the lower one; the bounds are loose because a single
  // periodogram of a random process is itself noisy.
  PinkNoise noise(1.0);
  const auto values = noise.take(2048);
  const double lower = test::band_power(values, 16, 32);
  const double upper = test::band_power(values, 32, 64);
  CHECK(lower > upper);
  CHECK(lower / upper == doctest::Approx(2.0).epsilon(0.6));

  SUBCASE("while white noise is flat across the same octaves") {
    const auto flat = WhiteNoise(1.0).take(2048);
    CHECK(test::band_power(flat, 16, 32) / test::band_power(flat, 32, 64) ==
          doctest::Approx(1.0).epsilon(0.5));
  }
}

TEST_CASE("changing sigma after construction rescales the output") {
  PinkNoise noise(1.0);
  noise.set_sigma(5.0);
  noise.reset();
  CHECK(noise.rms() == doctest::Approx(5.0));
  CHECK(test::stdev(noise.take(200000)) == doctest::Approx(5.0).epsilon(0.05));
}
