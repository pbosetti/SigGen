// SigGen -- golden-value tests shared with the Python wrapper.
//
// Copyright 2026 Paolo Bosetti
// SPDX-License-Identifier: Apache-2.0
//
// The literals below are asserted here and, byte for byte, in
// test/python/test_c_api.py. That pins two properties at once: that the
// generators keep producing the same numbers across releases, and that a
// stream reached through the C ABI and ctypes is the same stream the C++ API
// produces. Rng deliberately avoids <random>'s distribution classes for this
// reason, so the values also hold across platforms and standard libraries.
//
// If a deliberate change to a generator moves these numbers, update both files
// together -- a mismatch between them is the signal that something drifted.

#include <siggen/config.hpp>

#include <doctest/doctest.h>

#include <vector>

namespace {

std::vector<double> generate(const char *document, std::size_t n) {
  return SigGen::from_string(document)->take(n);
}

} // namespace

TEST_CASE("a noisy sine reproduces its documented samples") {
  const std::vector<double> expected = {-0.238320196013666, 0.451380277722327,
                                        1.43477757151572,   1.69979347159333,
                                        1.95174132247097,   1.80515020639691};
  const std::vector<double> actual = generate(
      R"({"sample_rate":100,"seed":12345,
          "signal":{"type":"sine","frequency":5,"amplitude":2,"snr_db":20}})",
      expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i)
    CHECK(actual[i] == doctest::Approx(expected[i]).epsilon(1e-12));
}

TEST_CASE("pink noise reproduces its documented samples") {
  const std::vector<double> expected = {-1.19245579401624, -1.10831409955108,
                                        -1.61931945027813, -1.68866352014714};
  const std::vector<double> actual =
      generate(R"({"seed":7,"signal":{"type":"pink_noise","sigma":1.0}})",
               expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i)
    CHECK(actual[i] == doctest::Approx(expected[i]).epsilon(1e-12));
}

TEST_CASE("an ARIMA process reproduces its documented samples") {
  const std::vector<double> expected = {-2.23437222690341, -3.44326070343336,
                                        -3.42982679109250, -2.79098226245950};
  const std::vector<double> actual = generate(
      R"({"seed":99,
          "signal":{"type":"arima","ar":[0.6],"d":1,"ma":[0.3],"sigma":1.0}})",
      expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i)
    CHECK(actual[i] == doctest::Approx(expected[i]).epsilon(1e-12));
}
