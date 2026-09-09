// Composition: summing, multiplying, ownership and propagation.
//
// Copyright 2026 Paolo Bosetti
// SPDX-License-Identifier: Apache-2.0

#include "test_common.hpp"

#include <doctest/doctest.h>
#include <siggen/composite.hpp>
#include <siggen/noise.hpp>
#include <siggen/periodic.hpp>

#include <numbers>

using namespace SigGen;

namespace {

Sine quiet_sine(double frequency, double amplitude, double phase = 0.0) {
  Sine sine(frequency, amplitude, phase);
  sine.set_noiseless();
  return sine;
}

} // namespace

TEST_CASE("a composite carries no intrinsic noise of its own") {
  CHECK_FALSE(Composite().snr_db().has_value());
}

TEST_CASE("an empty composite is silent") {
  Composite empty;
  CHECK(empty.empty());
  CHECK(empty.rms() == doctest::Approx(0.0));
  for (double y : empty.take(10))
    CHECK(y == doctest::Approx(0.0));
}

TEST_CASE("summing adds the components sample by sample") {
  Composite sum;
  sum.add(quiet_sine(50.0, 1.0));
  sum.add(quiet_sine(150.0, 0.3));
  sum.set_sample_rate(1000.0);

  Sine first = quiet_sine(50.0, 1.0), second = quiet_sine(150.0, 0.3);
  first.set_sample_rate(1000.0);
  second.set_sample_rate(1000.0);

  const auto combined = sum.take(200);
  const auto a = first.take(200), b = second.take(200);
  for (std::size_t i = 0; i < combined.size(); ++i)
    CHECK(combined[i] == doctest::Approx(a[i] + b[i]).epsilon(1e-12));
}

TEST_CASE("gains scale the components") {
  Composite sum;
  sum.add(quiet_sine(1.0, 1.0), 2.5);
  sum.set_sample_rate(4.0);
  CHECK(sum.gain(0) == doctest::Approx(2.5));
  const auto values = sum.take(2);
  CHECK(values[1] == doctest::Approx(2.5));
  sum.set_gain(0, -1.0);
  sum.reset();
  CHECK(sum.take(2)[1] == doctest::Approx(-1.0));
}

TEST_CASE("multiplying modulates one component by another") {
  Composite product(Composite::Op::MULTIPLY);
  product.add(quiet_sine(100.0, 1.0));
  product.add(quiet_sine(2.0, 1.0, std::numbers::pi / 2.0));
  product.set_sample_rate(1000.0);

  Sine carrier = quiet_sine(100.0, 1.0);
  Sine envelope = quiet_sine(2.0, 1.0, std::numbers::pi / 2.0);
  carrier.set_sample_rate(1000.0);
  envelope.set_sample_rate(1000.0);

  const auto modulated = product.take(200);
  const auto c = carrier.take(200), e = envelope.take(200);
  for (std::size_t i = 0; i < modulated.size(); ++i)
    CHECK(modulated[i] == doctest::Approx(c[i] * e[i]).epsilon(1e-12));
}

TEST_CASE("rms combines powers under sum and amplitudes under multiply") {
  Composite sum;
  sum.add(quiet_sine(50.0, 1.0));
  sum.add(quiet_sine(150.0, 0.3));
  CHECK(sum.rms() == doctest::Approx(std::sqrt(0.5 + 0.09 / 2.0)));

  Composite product(Composite::Op::MULTIPLY);
  product.add(quiet_sine(50.0, 1.0), 2.0);
  product.add(quiet_sine(150.0, 0.3));
  CHECK(product.rms() == doctest::Approx(2.0 * (1.0 / std::numbers::sqrt2) *
                                         (0.3 / std::numbers::sqrt2)));
}

TEST_CASE("the sampling rate reaches components added before and after") {
  Composite sum;
  sum.add(quiet_sine(1.0, 1.0));
  sum.set_sample_rate(250.0);
  sum.add(quiet_sine(1.0, 1.0));
  CHECK(sum.component(0).sample_rate() == doctest::Approx(250.0));
  CHECK(sum.component(1).sample_rate() == doctest::Approx(250.0));
  sum.set_sample_rate(500.0);
  CHECK(sum.component(0).sample_rate() == doctest::Approx(500.0));
  CHECK(sum.component(1).sample_rate() == doctest::Approx(500.0));
}

TEST_CASE("identical components are given distinct seeds") {
  // Two white-noise components seeded alike would double one stream instead of
  // adding two, which would show up as a sigma of 2 rather than sqrt(2).
  Composite sum;
  sum.add(WhiteNoise(1.0));
  sum.add(WhiteNoise(1.0));
  CHECK(sum.component(0).seed() != sum.component(1).seed());
  CHECK(test::stdev(sum.take(100000)) ==
        doctest::Approx(std::numbers::sqrt2).epsilon(0.03));
}

TEST_CASE("a composite is copied deeply") {
  Composite original;
  original.add(quiet_sine(10.0, 1.0));
  original.add(WhiteNoise(0.5));
  original.set_sample_rate(500.0);

  Composite copy = original;
  original.reset();
  copy.reset();
  const auto a = original.take(100), b = copy.take(100);
  for (std::size_t i = 0; i < a.size(); ++i)
    CHECK(a[i] == doctest::Approx(b[i]));

  SUBCASE("and the copy is independent of the original") {
    copy.set_gain(0, 0.0);
    original.reset();
    copy.reset();
    CHECK(original.take(50) != copy.take(50));
  }
}

TEST_CASE("composites nest") {
  Composite inner;
  inner.add(quiet_sine(1.0, 1.0));
  inner.add(quiet_sine(2.0, 0.5));

  Composite outer(Composite::Op::MULTIPLY);
  outer.add(inner);
  outer.add(quiet_sine(0.5, 1.0));
  outer.set_sample_rate(100.0);
  CHECK(outer.size() == 2);
  CHECK(outer.component(0).type() == "composite");
  CHECK(outer.take(100).size() == 100);
}

TEST_CASE("a composite rejects a null component and a bad index") {
  Composite sum;
  CHECK_THROWS_AS(sum.add(std::unique_ptr<Signal>()), SigGenException);
  CHECK_THROWS_AS(sum.component(0), SigGenException);
  sum.add(quiet_sine(1.0, 1.0));
  CHECK_THROWS_AS(sum.component(1), SigGenException);
}

TEST_CASE("cloning a composite through the base interface preserves it") {
  Composite sum;
  sum.add(quiet_sine(10.0, 1.0));
  sum.set_sample_rate(200.0);
  const std::unique_ptr<Signal> clone = sum.clone();
  CHECK(clone->type() == "composite");
  CHECK(clone->sample_rate() == doctest::Approx(200.0));
  CHECK(clone->rms() == doctest::Approx(sum.rms()));
}
