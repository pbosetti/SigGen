// Shared helpers for the SigGen test suite.
//
// Copyright 2026 Paolo Bosetti
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cmath>
#include <cstddef>
#include <numeric>
#include <vector>

namespace test {

inline double mean(const std::vector<double> &v) {
  return std::accumulate(v.begin(), v.end(), 0.0) /
         static_cast<double>(v.size());
}

inline double variance(const std::vector<double> &v) {
  const double m = mean(v);
  double sum = 0.0;
  for (double y : v)
    sum += (y - m) * (y - m);
  return sum / static_cast<double>(v.size());
}

inline double stdev(const std::vector<double> &v) {
  return std::sqrt(variance(v));
}

/// Root mean square about zero, which is what Signal::rms() reports.
inline double rms(const std::vector<double> &v) {
  double sum = 0.0;
  for (double y : v)
    sum += y * y;
  return std::sqrt(sum / static_cast<double>(v.size()));
}

/// Lag-1 autocorrelation, used as a cheap proxy for spectral slope.
inline double autocorrelation_1(const std::vector<double> &v) {
  const double m = mean(v);
  double numerator = 0.0, denominator = 0.0;
  for (std::size_t i = 0; i < v.size(); ++i) {
    denominator += (v[i] - m) * (v[i] - m);
    if (i > 0)
      numerator += (v[i] - m) * (v[i - 1] - m);
  }
  return numerator / denominator;
}

/// Mean power in a band, by direct evaluation of the DFT at each bin. Slow but
/// exact, and the test sizes are small enough that an FFT would be overkill.
inline double band_power(const std::vector<double> &v, std::size_t first_bin,
                         std::size_t last_bin) {
  const std::size_t n = v.size();
  const double two_pi = 6.283185307179586;
  double total = 0.0;
  for (std::size_t k = first_bin; k <= last_bin; ++k) {
    double real = 0.0, imaginary = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
      const double angle = two_pi * static_cast<double>(k) *
                           static_cast<double>(i) / static_cast<double>(n);
      real += v[i] * std::cos(angle);
      imaginary -= v[i] * std::sin(angle);
    }
    total += real * real + imaginary * imaginary;
  }
  return total / static_cast<double>(last_bin - first_bin + 1);
}

} // namespace test
