// Reproducibility: the property a testing library lives or dies by.
//
// Copyright 2026 Paolo Bosetti
// SPDX-License-Identifier: Apache-2.0

#include "test_common.hpp"

#include <doctest/doctest.h>
#include <siggen/arima.hpp>
#include <siggen/composite.hpp>
#include <siggen/noise.hpp>
#include <siggen/periodic.hpp>

using namespace SigGen;

TEST_CASE("the same seed gives the same stream") {
  WhiteNoise a(1.0), b(1.0);
  a.set_seed(99);
  b.set_seed(99);
  CHECK(a.take(1000) == b.take(1000));
}

TEST_CASE("different seeds give different streams") {
  WhiteNoise a(1.0), b(1.0);
  a.set_seed(1);
  b.set_seed(2);
  CHECK(a.take(1000) != b.take(1000));
}

TEST_CASE("reset rewinds the stream") {
  Sine sine(10.0, 1.0);
  sine.set_sample_rate(500.0);
  const auto first = sine.take(500);
  sine.reset();
  CHECK(sine.take(500) == first);
}

TEST_CASE("reset rewinds the clock as well as the noise") {
  Sine sine(1.0, 1.0);
  sine.set_sample_rate(100.0);
  sine.take(37);
  CHECK(sine.time() == doctest::Approx(0.37));
  CHECK(sine.index() == 37);
  sine.reset();
  CHECK(sine.time() == doctest::Approx(0.0));
  CHECK(sine.index() == 0);
}

TEST_CASE("reseeding rewinds filtered generators too") {
  // Pink and brown noise warm their filters up from the random stream, so a
  // reseed that left that state untouched would carry the old seed forward.
  PinkNoise pink(1.0);
  pink.take(500);
  pink.set_seed(7);
  const auto first = pink.take(500);
  pink.set_seed(7);
  CHECK(pink.take(500) == first);

  BrownNoise brown(1.0);
  brown.take(500);
  brown.set_seed(7);
  const auto brown_first = brown.take(500);
  brown.set_seed(7);
  CHECK(brown.take(500) == brown_first);
}

TEST_CASE("reseeding a composite reaches its components") {
  Composite sum;
  sum.add(WhiteNoise(1.0));
  sum.add(WhiteNoise(1.0));
  sum.set_seed(4242);
  const auto first = sum.take(500);
  sum.set_seed(4242);
  CHECK(sum.take(500) == first);

  SUBCASE("and reseeds them apart from one another") {
    CHECK(sum.component(0).seed() != sum.component(1).seed());
  }
}

TEST_CASE("an ARIMA process is reproducible across its warm-up") {
  Arima process({0.8}, 1, {0.3}, 1.0);
  process.set_seed(11);
  const auto first = process.take(500);
  process.set_seed(11);
  CHECK(process.take(500) == first);
}

TEST_CASE("a default-constructed signal is already deterministic") {
  CHECK(Sine(10.0).take(100) == Sine(10.0).take(100));
  CHECK(WhiteNoise(1.0).seed() == Rng::default_seed);
}

TEST_CASE("the stream does not depend on the standard library implementation") {
  // <random>'s distributions are free to draw differently on each standard
  // library, so SigGen derives its own uniforms and normals from the raw
  // Mersenne Twister output. These values pin that down: a change here means
  // the stream is no longer reproducible across platforms.
  Rng rng(12345);
  CHECK(rng.uniform() == doctest::Approx(0.35762972288842587).epsilon(1e-15));
  CHECK(rng.uniform() == doctest::Approx(0.40044261704406114).epsilon(1e-15));

  Rng normal(12345);
  CHECK(normal.gaussian() ==
        doctest::Approx(-1.6851782669497068).epsilon(1e-12));
  CHECK(normal.gaussian() ==
        doctest::Approx(-1.1784196917749634).epsilon(1e-12));
}
