// ARIMA processes: stationary statistics and the effect of integration.
//
// Copyright 2026 Paolo Bosetti
// SPDX-License-Identifier: Apache-2.0

#include "test_common.hpp"

#include <doctest/doctest.h>
#include <siggen/arima.hpp>

using namespace SigGen;

namespace {

std::vector<double> difference(const std::vector<double> &v) {
  std::vector<double> out;
  out.reserve(v.size() - 1);
  for (std::size_t i = 1; i < v.size(); ++i)
    out.push_back(v[i] - v[i - 1]);
  return out;
}

} // namespace

TEST_CASE("an ARIMA process carries no intrinsic noise of its own") {
  CHECK_FALSE(Arima({0.5}).snr_db().has_value());
}

TEST_CASE("an AR(1) process has the theoretical variance") {
  for (double phi : {0.3, 0.5, 0.9}) {
    Arima process({phi}, 0, {}, 1.0);
    const double theory = 1.0 / std::sqrt(1.0 - phi * phi);
    CHECK(process.rms() == doctest::Approx(theory).epsilon(1e-6));
    CHECK(test::stdev(process.take(200000)) ==
          doctest::Approx(theory).epsilon(0.05));
  }
}

TEST_CASE("an AR(1) process has the theoretical autocorrelation") {
  Arima process({0.7}, 0, {}, 1.0);
  CHECK(test::autocorrelation_1(process.take(200000)) ==
        doctest::Approx(0.7).epsilon(0.05));
}

TEST_CASE("an MA(1) process has the theoretical variance") {
  const double theta = 0.6;
  Arima process({}, 0, {theta}, 2.0);
  const double theory = 2.0 * std::sqrt(1.0 + theta * theta);
  CHECK(process.rms() == doctest::Approx(theory).epsilon(1e-9));
  CHECK(test::stdev(process.take(200000)) ==
        doctest::Approx(theory).epsilon(0.05));
}

TEST_CASE("pure innovations reduce to white noise") {
  Arima white({}, 0, {}, 1.5);
  CHECK(white.rms() == doctest::Approx(1.5));
  const auto values = white.take(100000);
  CHECK(test::stdev(values) == doctest::Approx(1.5).epsilon(0.02));
  CHECK(test::autocorrelation_1(values) ==
        doctest::Approx(0.0).scale(0.02).epsilon(1.0));
}

TEST_CASE("integration turns innovations into a random walk") {
  Arima walk({}, 1, {}, 1.0);
  const auto values = walk.take(100000);
  // Differencing once must recover the white innovations.
  const auto differenced = difference(values);
  CHECK(test::stdev(differenced) == doctest::Approx(1.0).epsilon(0.02));
  CHECK(test::autocorrelation_1(differenced) ==
        doctest::Approx(0.0).scale(0.02).epsilon(1.0));

  SUBCASE("and the walk's own spread grows without bound") {
    const std::vector<double> early(values.begin(), values.begin() + 2000);
    const std::vector<double> late(values.end() - 2000, values.end());
    CHECK(test::variance(late) > test::variance(early));
  }

  SUBCASE("so rms() reports the stationary, differenced process") {
    CHECK(walk.rms() == doctest::Approx(1.0));
  }
}

TEST_CASE("integrating twice needs two differences to recover the noise") {
  Arima process({}, 2, {}, 1.0);
  const auto values = process.take(50000);
  CHECK(test::stdev(difference(difference(values))) ==
        doctest::Approx(1.0).epsilon(0.05));
}

TEST_CASE("a full ARIMA(1,1,1) differences back to a stationary ARMA(1,1)") {
  Arima full({0.5}, 1, {0.4}, 1.0);
  Arima stationary({0.5}, 0, {0.4}, 1.0);
  CHECK(full.rms() == doctest::Approx(stationary.rms()));
  CHECK(test::stdev(difference(full.take(200000))) ==
        doctest::Approx(stationary.rms()).epsilon(0.05));
}

TEST_CASE("a non-stationary AR part reports an infinite RMS") {
  Arima explosive({1.5}, 0, {}, 1.0);
  CHECK(std::isinf(explosive.rms()));
}

TEST_CASE("an ARIMA process starts warmed up") {
  // Without a burn-in the first samples would sit near zero rather than in the
  // stationary distribution; compare the opening block against a later one.
  Arima process({0.95}, 0, {}, 1.0);
  const auto values = process.take(40000);
  const std::vector<double> first(values.begin(), values.begin() + 2000);
  const std::vector<double> last(values.end() - 2000, values.end());
  CHECK(test::stdev(first) == doctest::Approx(test::stdev(last)).epsilon(0.4));
}
