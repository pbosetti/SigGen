// SigGen -- periodic waveforms: sine, square, triangle and sawtooth.
//
// Copyright 2026 Paolo Bosetti
// SPDX-License-Identifier: Apache-2.0
//
// All four share a phase accumulator rather than evaluating a closed form in
// t. Accumulating means the frequency can be changed mid-stream without the
// phase jumping, and it keeps the waveform exact over long runs, where
// sin(2*pi*f*t) loses precision as t grows.

#pragma once

#include "signal.hpp"

#include <cmath>
#include <limits>
#include <numbers>
#include <string>

namespace SigGen {

/// Common base of the periodic waveforms.
///
/// Subclasses implement waveform(), mapping a phase in [0, 1) onto the
/// interval [-1, 1]; this class applies amplitude, offset and phase advance.
class Periodic : public Signal {
public:
  /// Frequency in hertz. Negative values run the waveform backwards.
  double frequency() const { return _frequency; }
  void set_frequency(double frequency) { _frequency = frequency; }

  /// Peak amplitude, before the offset is added.
  double amplitude() const { return _amplitude; }
  void set_amplitude(double amplitude) { _amplitude = amplitude; }

  /// Initial phase in radians. Takes effect on the next reset().
  double phase() const { return _phase; }
  void set_phase(double phase) {
    _phase = phase;
    _accumulator = wrap(phase / (2.0 * std::numbers::pi));
  }

  /// Constant added to every sample. Excluded from rms(), and therefore from
  /// the intrinsic noise level.
  double offset() const { return _offset; }
  void set_offset(double offset) { _offset = offset; }

  /// The period in seconds, or infinity at zero frequency.
  double period() const {
    return _frequency == 0.0 ? std::numeric_limits<double>::infinity()
                             : 1.0 / std::abs(_frequency);
  }

  void reset() override {
    Signal::reset();
    _accumulator = wrap(_phase / (2.0 * std::numbers::pi));
  }

protected:
  Periodic(double frequency, double amplitude, double phase, double offset)
      : _frequency(frequency), _amplitude(amplitude), _phase(phase),
        _offset(offset), _accumulator(wrap(phase / (2.0 * std::numbers::pi))) {}

  /// The unit waveform over one period: phase runs in [0, 1), the result in
  /// [-1, 1].
  virtual double waveform(double phase) const = 0;

  double sample() override {
    const double y = _amplitude * waveform(_accumulator) + _offset;
    _accumulator = wrap(_accumulator + _frequency / sample_rate());
    return y;
  }

  /// Fold a phase into [0, 1), for negative values too.
  static double wrap(double phase) { return phase - std::floor(phase); }

private:
  double _frequency;
  double _amplitude;
  double _phase;
  double _offset;
  double _accumulator;
}; // class Periodic

/// A sine wave. Its RMS is amplitude / sqrt(2).
class Sine : public CloneableSignal<Sine, Periodic> {
public:
  explicit Sine(double frequency = 1.0, double amplitude = 1.0,
                double phase = 0.0, double offset = 0.0)
      : CloneableSignal(frequency, amplitude, phase, offset) {}

  std::string type() const override { return "sine"; }

  double rms() const override {
    return std::abs(amplitude()) / std::numbers::sqrt2;
  }

protected:
  double waveform(double phase) const override {
    return std::sin(2.0 * std::numbers::pi * phase);
  }
}; // class Sine

/// A square wave, high for the first `duty` fraction of each period. Its RMS
/// is the amplitude whatever the duty cycle, since the wave only ever takes
/// the values +/- amplitude.
class Square : public CloneableSignal<Square, Periodic> {
public:
  explicit Square(double frequency = 1.0, double amplitude = 1.0,
                  double duty = 0.5, double phase = 0.0, double offset = 0.0)
      : CloneableSignal(frequency, amplitude, phase, offset) {
    set_duty(duty);
  }

  /// Fraction of the period spent high, in (0, 1).
  double duty() const { return _duty; }
  void set_duty(double duty) {
    if (!(duty > 0.0 && duty < 1.0))
      throw SigGenException("duty must lie in (0, 1), got " +
                            std::to_string(duty));
    _duty = duty;
  }

  std::string type() const override { return "square"; }

  double rms() const override { return std::abs(amplitude()); }

protected:
  double waveform(double phase) const override {
    return phase < _duty ? 1.0 : -1.0;
  }

private:
  double _duty = 0.5;
}; // class Square

/// A triangle wave rising over the first `symmetry` fraction of the period
/// and falling over the rest. Its RMS is amplitude / sqrt(3), independent of
/// the symmetry, because both segments are linear ramps spanning [-1, 1].
class Triangle : public CloneableSignal<Triangle, Periodic> {
public:
  explicit Triangle(double frequency = 1.0, double amplitude = 1.0,
                    double symmetry = 0.5, double phase = 0.0,
                    double offset = 0.0)
      : CloneableSignal(frequency, amplitude, phase, offset) {
    set_symmetry(symmetry);
  }

  /// Fraction of the period spent rising, in [0, 1]. At 1 the wave degenerates
  /// to a rising sawtooth, at 0 to a falling one.
  double symmetry() const { return _symmetry; }
  void set_symmetry(double symmetry) {
    if (!(symmetry >= 0.0 && symmetry <= 1.0))
      throw SigGenException("symmetry must lie in [0, 1], got " +
                            std::to_string(symmetry));
    _symmetry = symmetry;
  }

  std::string type() const override { return "triangle"; }

  double rms() const override {
    return std::abs(amplitude()) / std::numbers::sqrt3;
  }

protected:
  double waveform(double phase) const override {
    if (_symmetry <= 0.0)
      return 1.0 - 2.0 * phase;
    if (_symmetry >= 1.0)
      return 2.0 * phase - 1.0;
    if (phase < _symmetry)
      return 2.0 * phase / _symmetry - 1.0;
    return 1.0 - 2.0 * (phase - _symmetry) / (1.0 - _symmetry);
  }

private:
  double _symmetry = 0.5;
}; // class Triangle

/// A sawtooth ramping from -amplitude to +amplitude over each period, with a
/// discontinuity at the wrap. Its RMS is amplitude / sqrt(3).
class Sawtooth : public CloneableSignal<Sawtooth, Periodic> {
public:
  explicit Sawtooth(double frequency = 1.0, double amplitude = 1.0,
                    double phase = 0.0, double offset = 0.0)
      : CloneableSignal(frequency, amplitude, phase, offset) {}

  std::string type() const override { return "sawtooth"; }

  double rms() const override {
    return std::abs(amplitude()) / std::numbers::sqrt3;
  }

protected:
  double waveform(double phase) const override { return 2.0 * phase - 1.0; }
}; // class Sawtooth

} // namespace SigGen
