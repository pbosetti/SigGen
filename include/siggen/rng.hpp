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
//
// Two access modes live side by side. The *sequential* one (uniform(),
// gaussian()) walks the Mersenne Twister and is what next() consumes. The
// *indexed* one (uniform_at(), gaussian_at()) is random access: the deviate
// for a given index is computed straight from the seed and the index, with no
// state and no need to have drawn the ones before it. Epoch-anchored
// generation needs that, because a generator joining an already-running signal
// has to produce the sample belonging to an absolute index it never counted up
// to.
//
// The two modes are deliberately different sequences: adding the indexed one
// left the sequential one untouched, so seeds and golden values from before it
// existed still produce the very same numbers.

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

  /// \name Indexed access
  /// Deviates addressed by index rather than drawn in order. Cost is constant:
  /// nothing is iterated, so the deviate for index 10^12 is as cheap as the
  /// one for index 0.
  ///
  /// `stream` selects an independent sub-sequence for the same seed. It exists
  /// so that a generator drawing its own innovations cannot collide with the
  /// intrinsic noise floor Signal adds on top: both would otherwise read the
  /// same index and the "noise" would merely scale the signal.
  /// @{

  /// Uniform deviate in (0, 1) for the given index.
  double uniform_at(std::uint64_t index, std::uint64_t stream = 0) const {
    // Shifted off zero: gaussian_at() takes a logarithm of this, and an exact
    // zero -- however unlikely -- would give negative infinity.
    return (static_cast<double>(bits_at(index, stream) >> 11) + 0.5) *
           0x1.0p-53;
  }

  /// Standard normal deviate, N(0, 1), for the given index.
  double gaussian_at(std::uint64_t index, std::uint64_t stream = 0) const {
    // The trigonometric Box-Muller form, not the polar one gaussian() uses:
    // it consumes exactly two uniforms with no rejection loop, so the mapping
    // from index to deviate is fixed. The polar form draws an unpredictable
    // number of times and caches a spare, neither of which can be addressed.
    const double u1 = uniform_at(2 * index, stream);
    const double u2 = uniform_at(2 * index + 1, stream);
    return std::sqrt(-2.0 * std::log(u1)) *
           std::cos(2.0 * 3.14159265358979323846 * u2);
  }
  /// @}

private:
  /// SplitMix64's finalising mix. Strong avalanche, and the reason the
  /// generator below can be addressed: SplitMix64 advances its state by a
  /// fixed constant, so its n-th output is a closed-form function of n.
  static std::uint64_t finalize(std::uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }

  /// The 64 bits belonging to (index, stream) -- SplitMix64's output at that
  /// step, reached directly rather than by iterating to it.
  std::uint64_t bits_at(std::uint64_t index, std::uint64_t stream) const {
    const std::uint64_t key = _seed ^ (stream * 0xD1B5'4A32'D192'ED03ULL);
    return finalize(key + (index + 1) * 0x9E37'79B9'7F4A'7C15ULL);
  }

  std::uint64_t _seed = default_seed;
  std::mt19937_64 _engine{default_seed};
  double _spare = 0.0;
  bool _has_spare = false;
}; // class Rng

} // namespace SigGen
