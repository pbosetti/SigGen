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
//
// All three can also be addressed by absolute index. White noise trivially so,
// its samples being independent. Pink and brown are recursive, but they forget
// exponentially: the state at an index is fixed, to well within rounding, by a
// bounded stretch of innovations before it. Since indexed innovations are
// random access, replaying that stretch reconstructs the filter at any index
// for a cost that does not grow with the index.

#pragma once

#include "signal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

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

  bool is_addressable() const override { return true; }

protected:
  double sample() override { return _sigma * rng().gaussian(); }

  // Independent samples, so there is no history to rebuild.
  double sample_at(std::uint64_t index) const override {
    return _sigma * rng().gaussian_at(index);
  }

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
      filter_step(_state, _delayed, rng().gaussian());
  }

  bool is_addressable() const override { return true; }

protected:
  double sample() override {
    return _gain * filter_step(_state, _delayed, rng().gaussian());
  }

  /// Rebuilds the filter from a bounded run of innovations ending at `index`.
  ///
  /// The answer matches a replay from index zero to within double rounding, at
  /// a cost that does not grow with the index -- but that cost is a run of
  /// history_for(0.99886) innovations, tens of thousands of them, so reach for
  /// values_at() rather than this whenever more than one sample is wanted.
  double sample_at(std::uint64_t index) const override {
    return sample_at_range(index, 1).front();
  }

  /// Rebuilds once, then steps -- so a block costs one reconstruction rather
  /// than one per sample.
  std::vector<double> sample_at_range(std::uint64_t first,
                                      std::size_t count) const override {
    std::array<double, 6> state{};
    double delay_state = 0.0;
    const std::uint64_t history = reconstruction_samples();
    const std::uint64_t start = first > history ? first - history : 0;
    for (std::uint64_t i = start; i < first; ++i)
      filter_step(state, delay_state, rng().gaussian_at(i));

    std::vector<double> out;
    out.reserve(count);
    for (std::size_t k = 0; k < count; ++k)
      out.push_back(_gain * filter_step(state, delay_state,
                                        rng().gaussian_at(first + k)));
    return out;
  }

private:
  /// History needed to rebuild the state, set by the slowest section.
  static std::uint64_t reconstruction_samples() {
    double slowest = 0.0;
    for (double p : poles)
      slowest = std::max(slowest, std::abs(p));
    return history_for(slowest);
  }

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

  /// One filter step over caller-supplied state, so that the same arithmetic
  /// serves the running stream and a reconstruction from local variables.
  static double filter_step(std::array<double, 6> &state, double &delay_state,
                            double white) {
    double sum = 0.0;
    for (std::size_t i = 0; i < poles.size(); ++i) {
      state[i] = poles[i] * state[i] + gains[i] * white;
      sum += state[i];
    }
    sum += delay_state + direct * white;
    delay_state = delayed * white;
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
    for (std::uint64_t i = 0; i < warmup_samples(); ++i)
      _state = _leak * _state + _drive * rng().gaussian();
  }

  bool is_addressable() const override { return true; }

protected:
  double sample() override {
    _state = _leak * _state + _drive * rng().gaussian();
    return _state;
  }

  /// Rebuilds the walk from a bounded run of innovations ending at `index`.
  /// The leak is what makes this possible at all: a true integrator would
  /// remember every innovation ever drawn, whereas this one forgets on a
  /// timescale of 1/(1 - leak) samples. A leak set very close to 1 therefore
  /// buys longer memory at the price of a longer reconstruction.
  double sample_at(std::uint64_t index) const override {
    return sample_at_range(index, 1).front();
  }

  /// Rebuilds once, then steps.
  std::vector<double> sample_at_range(std::uint64_t first,
                                      std::size_t count) const override {
    const std::uint64_t history = history_for(_leak);
    const std::uint64_t start = first > history ? first - history : 0;
    double state = 0.0;
    for (std::uint64_t i = start; i < first; ++i)
      state = _leak * state + _drive * rng().gaussian_at(i);

    std::vector<double> out;
    out.reserve(count);
    for (std::size_t k = 0; k < count; ++k) {
      state = _leak * state + _drive * rng().gaussian_at(first + k);
      out.push_back(state);
    }
    return out;
  }

private:
  /// Innovations needed to reach the steady state, five time constants of the
  /// leak. The floor keeps a leak of exactly 1 -- which reset() would
  /// otherwise turn into an endless loop -- to a finite, if long, run.
  std::uint64_t warmup_samples() const {
    return static_cast<std::uint64_t>(5.0 / std::max(1.0 - _leak, 1e-6));
  }

  double _sigma;
  double _leak = 0.99;
  // Input scaling that makes the steady-state variance of the AR(1) recursion,
  // drive^2 / (1 - leak^2), come out at exactly sigma^2.
  double _drive = 0.0;
  double _state = 0.0;
}; // class BrownNoise

} // namespace SigGen
