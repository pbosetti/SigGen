// SigGen -- seeded pseudo-random source.
//
// Copyright 2026 Paolo Bosetti
// SPDX-License-Identifier: Apache-2.0
//
// The generator deliberately avoids <random>'s distribution classes: their
// output sequence is implementation-defined, so the same seed yields different
// numbers on libstdc++, libc++ and MSVC. Uniforms are extracted from the raw
// 64-bit Mersenne Twister output and normals are produced by the polar
// Box-Muller transform, which makes a stream reproducible across every
// supported platform -- the property a testing library most needs.

#pragma once

#include <cmath>
#include <cstdint>
#include <random>

namespace SigGen {

/// A reproducible source of uniform and Gaussian deviates.
///
/// The seed is stored, so reset() rewinds the stream to its beginning: two
/// runs of the same signal with the same seed produce bit-identical samples.
class Rng {
public:
  /// Seed used unless one is given explicitly.
  static constexpr std::uint64_t default_seed = 0x51'6E'DE'FA'17'5E'ED'01ULL;

  explicit Rng(std::uint64_t seed = default_seed) { set_seed(seed); }

  /// Reseed and rewind the stream.
  void set_seed(std::uint64_t seed) {
    _seed = seed;
    reset();
  }

  std::uint64_t seed() const { return _seed; }

  /// Rewind to the start of the stream for the current seed.
  void reset() {
    _engine.seed(_seed);
    _has_spare = false;
    _spare = 0.0;
  }

  /// Uniform deviate in [0, 1).
  double uniform() {
    // 53 significant bits, the most a double can hold: the canonical
    // conversion, and identical on every implementation of mt19937_64.
    return static_cast<double>(_engine() >> 11) * 0x1.0p-53;
  }

  /// Uniform deviate in [lo, hi).
  double uniform(double lo, double hi) { return lo + (hi - lo) * uniform(); }

  /// Standard normal deviate, N(0, 1).
  double gaussian() {
    // Marsaglia polar method: draws a point in the unit disc and returns two
    // independent normals, the second cached for the next call.
    if (_has_spare) {
      _has_spare = false;
      return _spare;
    }
    double u = 0.0, v = 0.0, s = 0.0;
    do {
      u = uniform(-1.0, 1.0);
      v = uniform(-1.0, 1.0);
      s = u * u + v * v;
    } while (s >= 1.0 || s == 0.0);
    const double f = std::sqrt(-2.0 * std::log(s) / s);
    _spare = v * f;
    _has_spare = true;
    return u * f;
  }

private:
  std::uint64_t _seed = default_seed;
  std::mt19937_64 _engine{default_seed};
  double _spare = 0.0;
  bool _has_spare = false;
}; // class Rng

} // namespace SigGen
