// SigGen -- white, pink and brown noise.
//
// Copyright 2026 Paolo Bosetti
// SPDX-License-Identifier: Apache-2.0
//
// All three are parameterised by the standard deviation of their output, so
// they compose predictably with the deterministic waveforms. Pink and brown
// are shaped by IIR filters whose steady-state variance is computed in closed
// form at construction, which is what lets `sigma` mean the same thing for
// every colour instead of being an arbitrary pre-filter gain.

#pragma once

#include "signal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>

namespace SigGen {

/// Gaussian white noise: flat spectrum, samples independent.
class WhiteNoise : public CloneableSignal<WhiteNoise> {
public:
  explicit WhiteNoise(double sigma = 1.0)
      : CloneableSignal(std::nullopt), _sigma(sigma) {}

  /// Standard deviation of the output.
  double sigma() const { return _sigma; }
  void set_sigma(double sigma) { _sigma = sigma; }

  std::string type() const override { return "white_noise"; }

  double rms() const override { return std::abs(_sigma); }

protected:
  double sample() override { return _sigma * rng().gaussian(); }

private:
  double _sigma;
}; // class WhiteNoise

/// Pink noise: power falling at roughly 3 dB per octave (1/f).
///
/// Uses Paul Kellet's refined method -- six one-pole sections driven by the
/// same white source, plus a direct and a one-sample-delayed term. Because
/// every section is a first-order IIR, the impulse response is a sum of
/// geometric sequences and the output variance has a closed form, so the
/// filter can be scaled exactly to the requested sigma rather than by a
/// hand-tuned constant.
class PinkNoise : public CloneableSignal<PinkNoise> {
public:
  explicit PinkNoise(double sigma = 1.0)
      : CloneableSignal(std::nullopt), _sigma(sigma) {
    reset();
  }

  double sigma() const { return _sigma; }
  void set_sigma(double sigma) {
    _sigma = sigma;
    _gain = _sigma / raw_sigma();
  }

  std::string type() const override { return "pink_noise"; }

  double rms() const override { return std::abs(_sigma); }

  void reset() override {
    Signal::reset();
    _state.fill(0.0);
    _delayed = 0.0;
    // The slowest pole has a time constant of about 1/(1 - 0.99886) samples;
    // running a few of those discards the transient, so the very first sample
    // already has the steady-state variance.
    for (std::size_t i = 0; i < warmup_samples; ++i)
      filter(rng().gaussian());
  }

protected:
  double sample() override { return _gain * filter(rng().gaussian()); }

private:
  /// Pole of each one-pole section.
  static constexpr std::array<double, 6> poles = {0.99886, 0.99332, 0.96900,
                                                  0.86650, 0.55000, -0.7616};
  /// Input gain of each section.
  static constexpr std::array<double, 6> gains = {
      0.0555179, 0.0750759, 0.1538520, 0.3104856, 0.5329522, -0.0168980};
  /// Gain of the direct white term.
  static constexpr double direct = 0.5362;
  /// Gain of the term delayed by one sample.
  static constexpr double delayed = 0.115926;
  static constexpr std::size_t warmup_samples = 5000;

  double filter(double white) {
    double sum = 0.0;
    for (std::size_t i = 0; i < poles.size(); ++i) {
      _state[i] = poles[i] * _state[i] + gains[i] * white;
      sum += _state[i];
    }
    sum += _delayed + direct * white;
    _delayed = delayed * white;
    return sum;
  }

  /// Standard deviation of the raw filter output for unit-variance white
  /// input.
  ///
  /// The impulse response is h[n] = sum_i g_i p_i^n, plus `direct` at n = 0
  /// and `delayed` at n = 1. Summing h[n]^2 over all n turns the geometric
  /// series into sum_i sum_j g_i g_j / (1 - p_i p_j); the two isolated taps
  /// contribute their own squares and cross terms.
  static double raw_sigma() {
    double energy = 0.0;
    for (std::size_t i = 0; i < poles.size(); ++i)
      for (std::size_t j = 0; j < poles.size(); ++j)
        energy += gains[i] * gains[j] / (1.0 - poles[i] * poles[j]);

    double h0 = 0.0, h1 = 0.0;
    for (std::size_t i = 0; i < poles.size(); ++i) {
      h0 += gains[i];
      h1 += gains[i] * poles[i];
    }
    energy += 2.0 * direct * h0 + direct * direct;
    energy += 2.0 * delayed * h1 + delayed * delayed;
    return std::sqrt(energy);
  }

  double _sigma;
  /// Scaling that turns the filter output into a stream of the requested sigma.
  double _gain = _sigma / raw_sigma();
  std::array<double, 6> _state{};
  double _delayed = 0.0;
}; // class PinkNoise

/// Brown (red) noise: power falling at roughly 6 dB per octave (1/f^2).
///
/// A pure integrator of white noise is a random walk with unbounded variance,
/// which would drift away over a long run and make `sigma` meaningless. This
/// uses a leaky integrator instead: it behaves as 1/f^2 above the leak's
/// corner frequency and settles to a finite, exactly known variance below it.
class BrownNoise : public CloneableSignal<BrownNoise> {
public:
  explicit BrownNoise(double sigma = 1.0, double leak = 0.99)
      : CloneableSignal(std::nullopt), _sigma(sigma) {
    set_leak(leak);
    reset();
  }

  double sigma() const { return _sigma; }
  void set_sigma(double sigma) {
    _sigma = sigma;
    _drive = _sigma * std::sqrt(1.0 - _leak * _leak);
  }

  /// Pole of the integrator, in [0, 1). Values closer to 1 push the corner
  /// lower and make the walk longer-memoried.
  double leak() const { return _leak; }
  void set_leak(double leak) {
    if (!(leak >= 0.0 && leak < 1.0))
      throw SigGenException("leak must lie in [0, 1), got " +
                            std::to_string(leak));
    _leak = leak;
    _drive = _sigma * std::sqrt(1.0 - _leak * _leak);
  }

  std::string type() const override { return "brown_noise"; }

  double rms() const override { return std::abs(_sigma); }

  void reset() override {
    Signal::reset();
    _state = 0.0;
    // Starting from zero, the walk needs a few time constants to reach its
    // steady-state spread; skip them so sigma holds from the first sample.
    const std::size_t warmup =
        static_cast<std::size_t>(5.0 / std::max(1.0 - _leak, 1e-6));
    for (std::size_t i = 0; i < warmup; ++i)
      _state = _leak * _state + _drive * rng().gaussian();
  }

protected:
  double sample() override {
    _state = _leak * _state + _drive * rng().gaussian();
    return _state;
  }

private:
  double _sigma;
  double _leak = 0.99;
  // Input scaling that makes the steady-state variance of the AR(1) recursion,
  // drive^2 / (1 - leak^2), come out at exactly sigma^2.
  double _drive = 0.0;
  double _state = 0.0;
}; // class BrownNoise

} // namespace SigGen
