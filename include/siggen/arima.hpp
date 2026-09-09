// SigGen -- ARIMA(p, d, q) processes.
//
// Copyright 2026 Paolo Bosetti
// SPDX-License-Identifier: Apache-2.0
//
// An ARMA recursion drives d stages of cumulative summation, which is exactly
// the "integrated" half of the acronym: differencing the output d times
// recovers the stationary ARMA series.

#pragma once

#include "signal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace SigGen {

/// An autoregressive integrated moving-average process.
///
/// The stationary part follows the usual convention
///
///     x[t] = sum_i phi[i] x[t-1-i] + e[t] + sum_j theta[j] e[t-1-j]
///
/// with e drawn from N(0, sigma^2); the result is then accumulated d times.
/// Like the noise generators, an ARIMA process is stochastic by construction
/// and so carries no intrinsic white-noise component of its own -- its
/// innovations already play that role.
class Arima : public CloneableSignal<Arima> {
public:
  /// @param ar    autoregressive coefficients phi, innermost lag first
  /// @param d     order of integration
  /// @param ma    moving-average coefficients theta, innermost lag first
  /// @param sigma standard deviation of the innovations
  explicit Arima(std::vector<double> ar = {}, std::size_t d = 0,
                 std::vector<double> ma = {}, double sigma = 1.0)
      : CloneableSignal(std::nullopt), _ar(std::move(ar)), _ma(std::move(ma)),
        _d(d), _sigma(sigma) {
    reset();
  }

  const std::vector<double> &ar() const { return _ar; }
  const std::vector<double> &ma() const { return _ma; }
  /// Order of integration, the "I" of ARIMA.
  std::size_t differencing() const { return _d; }
  /// Standard deviation of the innovations.
  double sigma() const { return _sigma; }

  std::string type() const override { return "arima"; }

  /// RMS of the *stationary* process, i.e. of the output differenced d times.
  ///
  /// An integrated process (d > 0) has no finite RMS -- its variance grows
  /// without bound -- so reporting the differenced series is the only
  /// meaningful answer, and it is the one the signal-to-noise ratio of an
  /// enclosing signal should be measured against.
  double rms() const override {
    // Variance via the MA(infinity) representation: psi[0] = 1 and
    // psi[j] = theta[j] + sum_i phi[i] psi[j-1-i], with the sum of psi^2
    // giving the variance in units of sigma^2. The series converges
    // geometrically for a stationary AR part; a non-stationary one is
    // reported as infinite rather than silently truncated.
    constexpr std::size_t terms = 4000;
    std::vector<double> psi(terms, 0.0);
    psi[0] = 1.0;
    double energy = 1.0;
    for (std::size_t j = 1; j < terms; ++j) {
      double value = j <= _ma.size() ? _ma[j - 1] : 0.0;
      for (std::size_t i = 0; i < _ar.size() && i < j; ++i)
        value += _ar[i] * psi[j - 1 - i];
      psi[j] = value;
      energy += value * value;
      if (!std::isfinite(energy))
        return std::numeric_limits<double>::infinity();
    }
    return std::abs(_sigma) * std::sqrt(energy);
  }

  void reset() override {
    Signal::reset();
    _past_values.assign(_ar.size(), 0.0);
    _past_errors.assign(_ma.size(), 0.0);
    _integrators.assign(_d, 0.0);
    // Started from all zeros the recursion is far from its stationary
    // distribution; burning samples lets the transient decay before the first
    // one is handed out. The integrators are zeroed afterwards so that an
    // integrated process still starts from the origin.
    for (std::size_t i = 0; i < warmup_samples; ++i)
      arma_step();
  }

protected:
  double sample() override {
    double y = arma_step();
    for (double &accumulator : _integrators) {
      accumulator += y;
      y = accumulator;
    }
    return y;
  }

private:
  static constexpr std::size_t warmup_samples = 1000;

  /// One step of the stationary ARMA recursion.
  double arma_step() {
    const double innovation = _sigma * rng().gaussian();
    double value = innovation;
    for (std::size_t i = 0; i < _ar.size(); ++i)
      value += _ar[i] * _past_values[i];
    for (std::size_t j = 0; j < _ma.size(); ++j)
      value += _ma[j] * _past_errors[j];

    if (!_past_values.empty()) {
      _past_values.push_front(value);
      _past_values.pop_back();
    }
    if (!_past_errors.empty()) {
      _past_errors.push_front(innovation);
      _past_errors.pop_back();
    }
    return value;
  }

  std::vector<double> _ar;
  std::vector<double> _ma;
  std::size_t _d;
  double _sigma;
  std::deque<double> _past_values;
  std::deque<double> _past_errors;
  std::vector<double> _integrators;
}; // class Arima

} // namespace SigGen
