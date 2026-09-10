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
//
// Alongside it sits an *epoch-anchored* mode, for generating one signal on
// several machines at once and having the results line up. next() counts from
// wherever the object happened to start, which is private to it; at(index)
// instead addresses the sample belonging to an absolute index measured from a
// shared epoch, so two generators that agree on the epoch, the sampling rate
// and the seed agree on every sample, whenever each of them started.
//
// Not every generator can do this equally cheaply, and is_addressable() says
// which can at all. See the class comment on at().

#pragma once

#include "rng.hpp"

#include <chrono>
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

/// Seconds since the Unix epoch, read from the system clock.
///
/// The system clock, not the steady one: only wall-clock time means the same
/// thing on two machines, whereas a steady clock's origin is arbitrary and
/// per-boot. The price is that it can be stepped by a time daemon; where that
/// matters, feed index_at() a clock that is slewed rather than stepped.
inline double unix_now() {
  return std::chrono::duration<double>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

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

  /// Reference instant that index 0 belongs to, in seconds since the Unix
  /// epoch. Defaults to 0, i.e. the Unix epoch itself -- so two generators
  /// that never touch it still agree, which is the point.
  double epoch() const { return _epoch; }

  /// Set the reference instant. Virtual so that a Composite can pass it on.
  virtual void set_epoch(double unix_seconds) { _epoch = unix_seconds; }

  /// The absolute sample index belonging to a wall-clock instant.
  ///
  /// This is the whole trick: derive the index from the clock rather than
  /// counting calls. A caller that is late skips indices and one that is early
  /// repeats them, but neither drifts out of step with another machine -- which
  /// a free-running counter driven by a jittery timer inevitably would.
  std::uint64_t index_at(double unix_seconds) const {
    const double elapsed = unix_seconds - _epoch;
    if (elapsed < 0.0)
      throw SigGenException("instant precedes the epoch by " +
                            std::to_string(-elapsed) + " s");
    return static_cast<std::uint64_t>(std::llround(elapsed * _sample_rate));
  }

  /// The wall-clock instant an absolute index belongs to, inverting
  /// index_at().
  double time_at(std::uint64_t index) const {
    return _epoch + static_cast<double>(index) / _sample_rate;
  }

  /// Whether at() works for this generator.
  ///
  /// True for the waveforms, white noise, the tabulated signals, the
  /// exponentially forgetting filters (pink and brown noise, a stationary
  /// ARMA) and any composite of those. False only where a sample genuinely
  /// depends on unboundedly much history: an ARIMA process with a nonzero
  /// order of integration is a cumulative sum, and its value at an index is a
  /// function of every innovation before it, so there is nothing to reconstruct
  /// from.
  virtual bool is_addressable() const { return false; }

  /// The sample belonging to an absolute index, independent of where the
  /// sequential stream has reached.
  ///
  /// Two generators sharing an epoch, a sampling rate and a seed return the
  /// same value here for the same index, so their outputs are in phase however
  /// far apart in time they were started. Leaves the sequential stream
  /// untouched, so next() and at() may be mixed freely.
  ///
  /// Cost depends on the generator. It is constant for the waveforms and white
  /// noise, and constant but larger for the filtered ones, which rebuild
  /// bounded history before answering. Throws where is_addressable() is false.
  double at(std::uint64_t index) const {
    double y = sample_at(index);
    if (_snr_db.has_value())
      y += _rng.gaussian_at(index, noise_stream) * noise_sigma();
    return y;
  }

  /// The samples for a run of consecutive indices.
  ///
  /// **Prefer this to a loop over at()** for anything but a single sample. The
  /// filtered generators answer at() by rebuilding their history, which for
  /// pink noise runs tens of thousands of innovations; asked for a block they
  /// rebuild once and then step, turning a cost of O(history x count) into
  /// O(history + count). Streaming in blocks is the intended shape for
  /// real-time use in any case.
  std::vector<double> values_at(std::uint64_t first, std::size_t count) const {
    std::vector<double> out = sample_at_range(first, count);
    if (_snr_db.has_value()) {
      const double sigma = noise_sigma();
      for (std::size_t i = 0; i < count; ++i)
        out[i] += _rng.gaussian_at(first + i, noise_stream) * sigma;
    }
    return out;
  }

  /// As values_at(), paired with the wall-clock instant of each sample.
  std::vector<std::pair<double, double>> at_range(std::uint64_t first,
                                                  std::size_t count) const {
    const std::vector<double> values = values_at(first, count);
    std::vector<std::pair<double, double>> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
      out.emplace_back(time_at(first + i), values[i]);
    return out;
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
  const Rng &rng() const { return _rng; }

  /// Sub-stream the intrinsic noise floor draws from. Generators draw their
  /// own innovations from stream 0, so keeping the noise on its own stream
  /// stops the two reading the same deviates -- which would make the "noise"
  /// a scaling of the signal rather than an addition to it.
  static constexpr std::uint64_t noise_stream = 1;

  /// The clean waveform at an absolute index, without the intrinsic noise.
  ///
  /// Overridden by every generator that can be addressed; the default stands
  /// for those that cannot, and reports so rather than returning something
  /// quietly wrong.
  virtual double sample_at(std::uint64_t) const {
    throw SigGenException("a '" + type() +
                          "' signal cannot be addressed by index");
  }

  /// The clean waveform over a run of consecutive indices.
  ///
  /// The default answers each index on its own, which is right for the
  /// generators whose sample_at() is already O(1). The filtered ones override
  /// it to rebuild their history once and then step, and must agree with
  /// sample_at() to the last bit while doing so -- otherwise a device
  /// streaming in blocks would drift away from one sampling single indices.
  virtual std::vector<double> sample_at_range(std::uint64_t first,
                                              std::size_t count) const {
    std::vector<double> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
      out.push_back(sample_at(first + i));
    return out;
  }

  /// Innovations of history that determine an exponentially forgetting
  /// filter's state to within double rounding, given its slowest pole.
  ///
  /// Five time constants -- the usual rule, and what reset() uses to settle a
  /// fresh stream -- leaves a residue of e^-5, some seven parts in a thousand.
  /// That is invisible in a warm-up but not here: a rebuilt state has to match
  /// one carried forward exactly, so the run has to be long enough for the
  /// forgotten tail to fall under the arithmetic itself.
  static std::uint64_t history_for(double pole) {
    constexpr double negligible = 1e-17;
    constexpr std::uint64_t cap = 400'000;
    const double magnitude = std::min(std::abs(pole), 0.9999);
    if (!(magnitude > 0.0))
      return 1;
    return std::min(cap, static_cast<std::uint64_t>(std::ceil(
                             std::log(negligible) / std::log(magnitude))));
  }

private:
  double _sample_rate = default_sample_rate;
  double _epoch = 0.0;
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
