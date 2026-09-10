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

  /// A stationary ARMA forgets exponentially and can be reconstructed at any
  /// index; integration cannot. A cumulative sum's value at an index is a
  /// function of every innovation before it, with no bounded stretch that
  /// determines it, so a nonzero order of integration rules addressing out.
  bool is_addressable() const override { return _d == 0; }

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

  /// Rebuilds a stationary ARMA from a bounded run of innovations ending at
  /// `index`, the same warm-up reset() uses being enough to settle it.
  double sample_at(std::uint64_t index) const override {
    if (_d != 0)
      throw SigGenException(
          "an ARIMA signal integrated " + std::to_string(_d) +
          " time(s) cannot be addressed by index: a cumulative sum depends on "
          "every innovation before it, so there is no bounded history to "
          "rebuild");

    return sample_at_range(index, 1).front();
  }

  /// Rebuilds once, then steps.
  std::vector<double> sample_at_range(std::uint64_t first,
                                      std::size_t count) const override {
    if (_d != 0)
      return {sample_at(first)}; // rethrows with the explanation above

    std::deque<double> values(_ar.size(), 0.0);
    std::deque<double> errors(_ma.size(), 0.0);
    const std::uint64_t history = reconstruction_samples();
    const std::uint64_t start = first > history ? first - history : 0;
    for (std::uint64_t i = start; i < first; ++i)
      arma_step(values, errors, _sigma * rng().gaussian_at(i));

    std::vector<double> out;
    out.reserve(count);
    for (std::size_t k = 0; k < count; ++k)
      out.push_back(
          arma_step(values, errors, _sigma * rng().gaussian_at(first + k)));
    return out;
  }

private:
  static constexpr std::size_t warmup_samples = 1000;

  /// History needed to rebuild the recursion. The sum of |phi| bounds the
  /// spectral radius of the AR part, which is what sets how fast it forgets;
  /// a moving-average term of order q contributes exactly q more.
  std::uint64_t reconstruction_samples() const {
    double radius = 0.0;
    for (double phi : _ar)
      radius += std::abs(phi);
    return history_for(radius) + _ma.size();
  }

  /// One step of the stationary ARMA recursion over caller-supplied state, so
  /// that the same arithmetic serves the running stream and a reconstruction
  /// from local variables.
  double arma_step(std::deque<double> &values, std::deque<double> &errors,
                   double innovation) const {
    double value = innovation;
    for (std::size_t i = 0; i < _ar.size(); ++i)
      value += _ar[i] * values[i];
    for (std::size_t j = 0; j < _ma.size(); ++j)
      value += _ma[j] * errors[j];

    if (!values.empty()) {
      values.push_front(value);
      values.pop_back();
    }
    if (!errors.empty()) {
      errors.push_front(innovation);
      errors.pop_back();
    }
    return value;
  }

  /// The running stream's step, over the member state.
  double arma_step() {
    return arma_step(_past_values, _past_errors, _sigma * rng().gaussian());
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
