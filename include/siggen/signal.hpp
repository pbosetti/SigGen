// SigGen -- the Signal interface every generator implements.
//
// Copyright 2026 Paolo Bosetti
// SPDX-License-Identifier: Apache-2.0
//
// A signal is a stateful stream: next() yields one sample and advances the
// internal clock by one sampling period. Purely periodic waveforms could be
// evaluated at an arbitrary t, but ARIMA processes and coloured noise cannot
// -- each of their samples depends on the ones before it -- so the sequential
// model is the only one that covers every generator uniformly.

#pragma once

#include "rng.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace SigGen {

/// Thrown on any configuration failure: an unknown signal type, an
/// out-of-range parameter, or a malformed JSON document.
class SigGenException : public std::exception {
public:
  explicit SigGenException(std::string message)
      : _message(std::move(message)) {}
  const char *what() const noexcept override { return _message.c_str(); }

private:
  std::string _message;
}; // class SigGenException

/// Default sampling frequency, in hertz, for a freshly constructed signal.
inline constexpr double default_sample_rate = 1000.0;

/// Default signal-to-noise ratio, in decibels, of the white-noise component
/// every deterministic generator carries. A perfect waveform is never what a
/// real acquisition chain produces, so the library adds a little noise unless
/// explicitly told not to.
inline constexpr double default_snr_db = 40.0;

/// Abstract base of every generator.
///
/// Subclasses implement sample(), which returns the clean waveform value at
/// the current instant. The non-virtual next() wraps it, mixing in the
/// intrinsic white-noise component and advancing the clock, so the noise
/// policy lives in exactly one place.
class Signal {
public:
  virtual ~Signal() = default;

  /// One sample, advancing the internal clock by 1 / sample_rate().
  double next() {
    double y = sample();
    if (_snr_db.has_value())
      y += _rng.gaussian() * noise_sigma();
    ++_n;
    return y;
  }

  /// Rewind the clock and the random stream to their initial state.
  virtual void reset() {
    _n = 0;
    _rng.reset();
  }

  /// Lower-case identifier, matching the "type" key of the JSON schema.
  virtual std::string type() const = 0;

  /// A polymorphic copy, used when composing signals.
  virtual std::unique_ptr<Signal> clone() const = 0;

  /// Root mean square of the clean waveform, excluding any DC offset and the
  /// intrinsic noise. This is what the signal-to-noise ratio is measured
  /// against, which is why it is analytic rather than sampled.
  virtual double rms() const = 0;

  double sample_rate() const { return _sample_rate; }

  /// Set the sampling frequency in hertz. Virtual so that a Composite can
  /// propagate it to its components.
  virtual void set_sample_rate(double fs) {
    if (!(fs > 0.0))
      throw SigGenException("sample_rate must be positive, got " +
                            std::to_string(fs));
    _sample_rate = fs;
  }

  /// Time of the sample next() is about to produce, in seconds.
  double time() const { return static_cast<double>(_n) / _sample_rate; }

  /// The sampling period, in seconds.
  double time_step() const { return 1.0 / _sample_rate; }

  /// Index of the sample next() is about to produce.
  std::uint64_t index() const { return _n; }

  /// Reseed the random stream and rewind the signal. Reseeding implies a
  /// reset: a filtered generator warms its state up from the random stream, so
  /// keeping that state across a reseed would leave it derived from the old
  /// seed. Virtual so that a Composite can give each component a distinct,
  /// derived seed.
  virtual void set_seed(std::uint64_t seed) {
    _rng.set_seed(seed);
    reset();
  }

  std::uint64_t seed() const { return _rng.seed(); }

  /// Signal-to-noise ratio in decibels, or nullopt when the intrinsic noise
  /// is switched off.
  std::optional<double> snr_db() const { return _snr_db; }

  void set_snr_db(std::optional<double> snr_db) { _snr_db = snr_db; }

  /// Switch the intrinsic white-noise component off.
  void set_noiseless() { _snr_db.reset(); }

  /// Standard deviation of the intrinsic noise, derived from rms() and the
  /// signal-to-noise ratio. Zero when the noise is switched off.
  double noise_sigma() const {
    if (!_snr_db.has_value())
      return 0.0;
    return rms() / std::pow(10.0, *_snr_db / 20.0);
  }

  /// n samples, advancing the clock by n periods.
  std::vector<double> take(std::size_t n) {
    std::vector<double> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
      out.push_back(next());
    return out;
  }

  /// n (time, value) pairs, with time in seconds.
  std::vector<std::pair<double, double>> take_series(std::size_t n) {
    std::vector<std::pair<double, double>> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
      const double t = time();
      out.emplace_back(t, next());
    }
    return out;
  }

  /// Samples covering the given duration in seconds, rounded to the nearest
  /// whole sample.
  std::vector<double> take_for(double seconds) {
    if (seconds < 0.0)
      throw SigGenException("duration must not be negative, got " +
                            std::to_string(seconds));
    return take(static_cast<std::size_t>(std::llround(seconds * _sample_rate)));
  }

protected:
  Signal() = default;

  /// Constructor for generators that are stochastic in their own right and so
  /// carry no intrinsic noise: pass std::nullopt.
  explicit Signal(std::optional<double> snr_db) : _snr_db(snr_db) {}

  /// The clean waveform value at time(), without the intrinsic noise.
  /// Implementations may advance their own internal state, but must not touch
  /// the clock -- next() owns that.
  virtual double sample() = 0;

  /// The random stream, for subclasses that are themselves stochastic.
  Rng &rng() { return _rng; }

private:
  double _sample_rate = default_sample_rate;
  std::uint64_t _n = 0;
  Rng _rng;
  std::optional<double> _snr_db = default_snr_db;
}; // class Signal

/// CRTP helper supplying clone() so that concrete generators need only be
/// copy-constructible.
template <typename Derived, typename Base = Signal>
class CloneableSignal : public Base {
public:
  using Base::Base;

  std::unique_ptr<Signal> clone() const override {
    return std::make_unique<Derived>(static_cast<const Derived &>(*this));
  }
}; // class CloneableSignal

} // namespace SigGen
