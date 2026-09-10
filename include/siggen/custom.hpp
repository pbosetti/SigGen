// SigGen -- user-defined signals given as a table of time-value pairs.
//
// Copyright 2026 Paolo Bosetti
// SPDX-License-Identifier: Apache-2.0
//
// The table is resampled at the signal's own sampling rate, so the knots need
// not line up with sample instants and a coarse table can drive a fast stream.

#pragma once

#include "signal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace SigGen {

/// A signal interpolated from an explicit table of (time, value) points.
class Custom : public CloneableSignal<Custom> {
public:
  /// How values between two knots are computed.
  enum class Interp {
    LINEAR, ///< Straight line between neighbouring knots.
    STEP,   ///< Hold the value of the preceding knot.
    CUBIC,  ///< Cubic Hermite spline with central-difference tangents.
  };

  /// What happens outside the span covered by the table.
  enum class Extrap {
    /// Loop the table, treating start_time()..end_time() as exactly one
    /// period. The last knot is therefore the wrap point rather than a
    /// sample in its own right: to loop seamlessly, give the table a final
    /// knot repeating the first value at the end of the intended period.
    REPEAT,
    CLAMP, ///< Hold the first and last values indefinitely.
    ZERO,  ///< Emit zero outside the span.
  };

  using Point = std::pair<double, double>;

  /// Build from a table of points. The points are sorted by time; two knots
  /// sharing a time are rejected, since the value there would be ambiguous.
  explicit Custom(std::vector<Point> points, Interp interp = Interp::LINEAR,
                  Extrap extrap = Extrap::REPEAT)
      : _points(std::move(points)), _interp(interp), _extrap(extrap) {
    if (_points.empty())
      throw SigGenException("a custom signal needs at least one point");
    std::sort(_points.begin(), _points.end(),
              [](const Point &a, const Point &b) { return a.first < b.first; });
    for (std::size_t i = 1; i < _points.size(); ++i)
      if (_points[i].first == _points[i - 1].first)
        throw SigGenException("duplicate time " +
                              std::to_string(_points[i].first) +
                              " in a custom signal");
    _tangents = compute_tangents();
  }

  const std::vector<Point> &points() const { return _points; }

  Interp interpolation() const { return _interp; }
  void set_interpolation(Interp interp) { _interp = interp; }

  Extrap extrapolation() const { return _extrap; }
  void set_extrapolation(Extrap extrap) { _extrap = extrap; }

  /// First and last knot times.
  double start_time() const { return _points.front().first; }
  double end_time() const { return _points.back().first; }
  /// The span the table covers, which is also its period under Extrap::REPEAT.
  double span() const { return end_time() - start_time(); }

  /// The table's value at an arbitrary time, independent of the stream
  /// position. Exact at the knots for every interpolation mode.
  double value_at(double t) const {
    if (_points.size() == 1)
      return _extrap == Extrap::ZERO && t != start_time() ? 0.0
                                                          : _points[0].second;

    const double t0 = start_time(), t1 = end_time();
    if (t < t0 || t > t1) {
      switch (_extrap) {
      case Extrap::ZERO:
        return 0.0;
      case Extrap::CLAMP:
        return t < t0 ? _points.front().second : _points.back().second;
      case Extrap::REPEAT: {
        // fmod alone would keep the sign of a negative offset, so fold the
        // remainder back into [0, span) before shifting.
        double offset = std::fmod(t - t0, span());
        if (offset < 0.0)
          offset += span();
        t = t0 + offset;
        break;
      }
      }
    }

    // The first knot strictly after t; its predecessor opens the segment.
    const auto upper = std::upper_bound(
        _points.begin(), _points.end(), t,
        [](double value, const Point &p) { return value < p.first; });
    if (upper == _points.begin())
      return _points.front().second;
    if (upper == _points.end())
      return _points.back().second;

    const std::size_t hi = static_cast<std::size_t>(upper - _points.begin());
    const std::size_t lo = hi - 1;
    const double h = _points[hi].first - _points[lo].first;
    const double s = (t - _points[lo].first) / h;

    switch (_interp) {
    case Interp::STEP:
      return _points[lo].second;
    case Interp::LINEAR:
      return _points[lo].second + s * (_points[hi].second - _points[lo].second);
    case Interp::CUBIC:
      break;
    }

    // Cubic Hermite basis on the unit interval.
    const double s2 = s * s, s3 = s2 * s;
    const double h00 = 2.0 * s3 - 3.0 * s2 + 1.0;
    const double h10 = s3 - 2.0 * s2 + s;
    const double h01 = -2.0 * s3 + 3.0 * s2;
    const double h11 = s3 - s2;
    return h00 * _points[lo].second + h10 * h * _tangents[lo] +
           h01 * _points[hi].second + h11 * h * _tangents[hi];
  }

  std::string type() const override { return "custom"; }

  /// RMS of the tabulated waveform over its span, by trapezoidal integration
  /// of the squared value -- which respects unevenly spaced knots, unlike a
  /// plain mean over the knot values.
  double rms() const override {
    if (_points.size() == 1 || span() <= 0.0)
      return std::abs(_points.front().second);
    double integral = 0.0;
    for (std::size_t i = 1; i < _points.size(); ++i) {
      const double a = _points[i - 1].second, b = _points[i].second;
      integral +=
          0.5 * (a * a + b * b) * (_points[i].first - _points[i - 1].first);
    }
    return std::sqrt(integral / span());
  }

  bool is_addressable() const override { return true; }

protected:
  double sample() override { return value_at(time()); }

  /// The table is already a function of time, so addressing it is just a
  /// matter of converting the index -- elapsed time since the epoch, which is
  /// what the knot times are measured against.
  double sample_at(std::uint64_t index) const override {
    return value_at(static_cast<double>(index) / sample_rate());
  }

private:
  /// Central-difference slopes at the knots, one-sided at the two ends. Using
  /// real time differences rather than knot indices keeps the spline sane when
  /// the table is unevenly spaced.
  std::vector<double> compute_tangents() const {
    const std::size_t n = _points.size();
    std::vector<double> m(n, 0.0);
    if (n < 2)
      return m;
    for (std::size_t i = 0; i < n; ++i) {
      const std::size_t lo = (i == 0) ? 0 : i - 1;
      const std::size_t hi = (i == n - 1) ? n - 1 : i + 1;
      m[i] = (_points[hi].second - _points[lo].second) /
             (_points[hi].first - _points[lo].first);
    }
    return m;
  }

  std::vector<Point> _points;
  Interp _interp;
  Extrap _extrap;
  std::vector<double> _tangents;
}; // class Custom

} // namespace SigGen
