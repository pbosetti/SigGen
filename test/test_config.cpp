// JSON configuration, including the Expressionist substitution pass.
//
// Copyright 2026 Paolo Bosetti
// SPDX-License-Identifier: Apache-2.0

#include "test_common.hpp"

#include <doctest/doctest.h>
#include <siggen/config.hpp>

#include <numbers>
#include <string>

using namespace SigGen;

TEST_CASE("a bare signal object builds directly") {
  const auto signal = from_string(R"({"type": "sine", "frequency": 50})");
  CHECK(signal->type() == "sine");
  CHECK(signal->rms() == doctest::Approx(1.0 / std::numbers::sqrt2));
}

TEST_CASE("the wrapper form carries the stream settings") {
  const auto signal = from_string(R"({
    "sample_rate": 8000,
    "seed": 1234,
    "signal": {"type": "sine", "frequency": 50, "amplitude": 2}
  })");
  CHECK(signal->sample_rate() == doctest::Approx(8000.0));
  CHECK(signal->seed() == 1234);
  CHECK(signal->rms() == doctest::Approx(2.0 / std::numbers::sqrt2));
}

TEST_CASE("every waveform parameter round-trips") {
  const auto signal = from_string(R"({
    "type": "square", "frequency": 10, "amplitude": 3,
    "duty": 0.25, "offset": 1.5, "noiseless": true
  })");
  signal->set_sample_rate(40.0);
  const auto values = signal->take(4);
  CHECK(values[0] == doctest::Approx(4.5));
  CHECK(values[1] == doctest::Approx(-1.5));
  CHECK(values[2] == doctest::Approx(-1.5));
  CHECK(values[3] == doctest::Approx(-1.5));
}

TEST_CASE("expressions in parameters are evaluated") {
  const auto signal = from_string(R"({
    "f0": 50,
    "signal": {"type": "sine", "frequency": "$3 * f0", "amplitude": "$pi / 4"}
  })");
  signal->set_noiseless();
  signal->set_sample_rate(600.0);
  // 150 Hz at 600 samples per second is four samples per period.
  const auto values = signal->take(4);
  CHECK(values[0] == doctest::Approx(0.0));
  CHECK(values[1] == doctest::Approx(std::numbers::pi / 4.0));
  CHECK(values[2] == doctest::Approx(0.0).scale(1.0));
  CHECK(values[3] == doctest::Approx(-std::numbers::pi / 4.0));
}

TEST_CASE("a malformed expression is reported as a configuration error") {
  CHECK_THROWS_AS(from_string(R"({"type": "sine", "frequency": "$nope + 1"})"),
                  SigGenException);
  CHECK_THROWS_AS(
      from_string(R"({"a": "$b", "b": "$a", "type": "sine", "frequency": 1})"),
      SigGenException);
}

TEST_CASE("noise types build from JSON") {
  CHECK(from_string(R"({"type": "white_noise", "sigma": 2})")->rms() ==
        doctest::Approx(2.0));
  CHECK(from_string(R"({"type": "pink", "sigma": 0.5})")->rms() ==
        doctest::Approx(0.5));
  const auto brown =
      from_string(R"({"type": "brown", "sigma": 1, "leak": 0.5})");
  CHECK(brown->type() == "brown_noise");
  CHECK_FALSE(brown->snr_db().has_value());
}

TEST_CASE("a custom signal accepts a list of pairs") {
  const auto signal = from_string(R"({
    "type": "custom",
    "points": [[0, 0], [1, 10], [2, 0]],
    "interpolation": "linear",
    "extrapolation": "clamp",
    "noiseless": true
  })");
  signal->set_sample_rate(2.0);
  const auto values = signal->take(3);
  CHECK(values[0] == doctest::Approx(0.0));
  CHECK(values[1] == doctest::Approx(5.0));
  CHECK(values[2] == doctest::Approx(10.0));
}

TEST_CASE("a custom signal accepts parallel arrays, ranges included") {
  // The range operator is Expressionist's; using it for the time base is the
  // reason the two-array form exists alongside the list of pairs.
  const auto signal = from_string(R"({
    "type": "custom",
    "t": "$0:1:0.5",
    "y": [0, 4, 8],
    "extrapolation": "clamp",
    "noiseless": true
  })");
  signal->set_sample_rate(2.0);
  const auto values = signal->take(3);
  CHECK(values[0] == doctest::Approx(0.0));
  CHECK(values[1] == doctest::Approx(4.0));
  CHECK(values[2] == doctest::Approx(8.0));
}

TEST_CASE("mismatched parallel arrays are rejected") {
  CHECK_THROWS_AS(from_string(R"({"type":"custom","t":[0,1],"y":[0]})"),
                  SigGenException);
}

TEST_CASE("an ARIMA process builds from JSON") {
  const auto signal =
      from_string(R"({"type":"arima","ar":[0.5],"d":1,"ma":[0.2],"sigma":2})");
  CHECK(signal->type() == "arima");
  CHECK(signal->rms() == doctest::Approx(Arima({0.5}, 1, {0.2}, 2.0).rms()));
}

TEST_CASE("a fractional order of integration is rejected") {
  CHECK_THROWS_AS(from_string(R"({"type":"arima","d":1.5})"), SigGenException);
  CHECK_THROWS_AS(from_string(R"({"type":"arima","d":-1})"), SigGenException);
}

TEST_CASE("a composite builds from JSON, gains included") {
  const auto signal = from_string(R"({
    "sample_rate": 1000,
    "signal": {
      "type": "composite", "op": "sum",
      "components": [
        {"type": "sine", "frequency": 50, "amplitude": 1, "noiseless": true},
        {"type": "sine", "frequency": 150, "amplitude": 1, "gain": 0.3,
         "noiseless": true}
      ]
    }
  })");
  CHECK(signal->type() == "composite");
  CHECK(signal->rms() == doctest::Approx(std::sqrt(0.5 + 0.09 / 2.0)));
  CHECK(signal->sample_rate() == doctest::Approx(1000.0));
}

TEST_CASE("a multiplying composite builds from JSON") {
  const auto signal = from_string(R"({
    "type": "composite", "op": "multiply",
    "components": [{"type":"sine","frequency":100},
                   {"type":"sine","frequency":2}]
  })");
  CHECK(signal->take(10).size() == 10);
}

TEST_CASE("the noise floor is configurable from JSON") {
  CHECK(from_string(R"({"type":"sine","frequency":1})")->snr_db().value() ==
        doctest::Approx(default_snr_db));
  CHECK(from_string(R"({"type":"sine","frequency":1,"snr_db":12})")
            ->snr_db()
            .value() == doctest::Approx(12.0));
  CHECK_FALSE(from_string(R"({"type":"sine","frequency":1,"snr_db":null})")
                  ->snr_db()
                  .has_value());
  CHECK_FALSE(from_string(R"({"type":"sine","frequency":1,"noiseless":true})")
                  ->snr_db()
                  .has_value());
}

TEST_CASE("configuration errors name the offending node") {
  const auto message = [](const std::string &text) {
    try {
      from_string(text);
    } catch (const SigGenException &e) {
      return std::string(e.what());
    }
    return std::string("no error");
  };

  CHECK(message(R"({"type": "nope"})").find("/type") != std::string::npos);
  CHECK(message(R"({"type": "nope"})").find("nope") != std::string::npos);
  CHECK(message(R"({"type": "sine"})").find("frequency") != std::string::npos);
  CHECK(message(R"({"type":"composite","components":[{"type":"sine"}]})")
            .find("/components/0") != std::string::npos);
  CHECK(message(R"({"type":"sine","frequency":"nope"})").find("/frequency") !=
        std::string::npos);
}

TEST_CASE("invalid JSON text is reported as a configuration error") {
  CHECK_THROWS_AS(from_string("{not json"), SigGenException);
}

TEST_CASE("a missing file is reported rather than crashing") {
  CHECK_THROWS_AS(from_file("no/such/file.json"), SigGenException);
}
