// SigGen -- epoch anchoring and index addressing.
//
// Copyright 2026 Paolo Bosetti
// SPDX-License-Identifier: Apache-2.0
//
// The property under test throughout: two generators built from the same
// description, started at unrelated moments, return the same sample for the
// same absolute index. That is what lets one signal be produced on several
// machines at once and line up.

#include <siggen/config.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <memory>
#include <vector>

namespace {

constexpr double epoch = 1767225600.0; // 2026-01-01T00:00:00Z
constexpr double day = 86400.0;

std::unique_ptr<SigGen::Signal> build(const char *document) {
  auto signal = SigGen::from_string(document);
  signal->set_epoch(epoch);
  return signal;
}

} // namespace

TEST_CASE("an index maps to a wall-clock instant and back") {
  auto signal =
      build(R"({"sample_rate":1000,"signal":{"type":"sine","frequency":1}})");
  CHECK(signal->epoch() == epoch);
  CHECK(signal->index_at(epoch) == 0);
  CHECK(signal->index_at(epoch + 1.0) == 1000);
  CHECK(signal->index_at(epoch + day) == 86'400'000);
  CHECK(signal->time_at(1000) == doctest::Approx(epoch + 1.0));
  CHECK(signal->time_at(signal->index_at(epoch + 12.5)) ==
        doctest::Approx(epoch + 12.5));
}

TEST_CASE("an index is the nearest one, so a jittery caller does not drift") {
  auto signal =
      build(R"({"sample_rate":1000,"signal":{"type":"sine","frequency":1}})");
  // A millisecond grid: anything within half a period lands on the same index.
  CHECK(signal->index_at(epoch + 5.0) == 5000);
  CHECK(signal->index_at(epoch + 5.0 + 0.0004) == 5000);
  CHECK(signal->index_at(epoch + 5.0 - 0.0004) == 5000);
  CHECK(signal->index_at(epoch + 5.0 + 0.0006) == 5001);
}

TEST_CASE("an instant before the epoch is refused rather than wrapped") {
  auto signal = build(R"({"signal":{"type":"sine","frequency":1}})");
  CHECK_THROWS_AS(signal->index_at(epoch - 1.0), SigGen::SigGenException);
}

TEST_CASE("two generators started apart agree at the same index") {
  const char *document = R"({"sample_rate":1000,"seed":42,"signal":{
      "type":"composite","op":"sum","components":[
        {"type":"sine","frequency":50,"amplitude":1.0,"snr_db":30},
        {"type":"sine","frequency":150,"amplitude":0.3},
        {"type":"pink_noise","sigma":0.05},
        {"type":"brown_noise","sigma":0.02},
        {"type":"white_noise","sigma":0.01}]}})";

  auto early = build(document);
  auto late = build(document);
  REQUIRE(early->is_addressable());

  // One has been running for a while; the other has produced nothing at all.
  early->take(12345);

  const std::uint64_t index = early->index_at(epoch + 3 * day + 7.0);
  for (std::uint64_t k = 0; k < 200; ++k)
    CHECK(early->at(index + k) == late->at(index + k));
}

TEST_CASE("addressing leaves the sequential stream alone") {
  auto signal =
      build(R"({"sample_rate":100,"seed":9,"signal":{"type":"pink_noise"}})");
  const std::vector<double> expected = signal->take(32);
  signal->reset();

  const std::vector<double> interleaved = [&] {
    std::vector<double> out;
    for (int i = 0; i < 32; ++i) {
      signal->at(1'000'000 + i); // must not disturb what follows
      out.push_back(signal->next());
    }
    return out;
  }();
  CHECK(interleaved == expected);
}

TEST_CASE("a block and single samples agree to the last bit") {
  // The two paths through the filtered generators -- rebuild once and step, or
  // rebuild per sample -- have to give identical answers, or a device
  // streaming blocks would drift from one sampling single indices.
  for (
      const char *document :
      {R"({"seed":3,"signal":{"type":"pink_noise","sigma":2}})",
       R"({"seed":4,"signal":{"type":"brown_noise","sigma":1.5}})",
       R"({"seed":5,"signal":{"type":"arima","ar":[0.7],"ma":[0.2],"sigma":1}})",
       R"({"seed":6,"signal":{"type":"composite","components":[
              {"type":"sine","frequency":7,"snr_db":25},
              {"type":"pink_noise","sigma":0.3}]}})"}) {
    auto blocked = build(document);
    auto single = build(document);
    const std::uint64_t first = 5'000'000'000ULL;
    const std::vector<double> block = blocked->values_at(first, 64);
    REQUIRE(block.size() == 64);
    for (std::size_t i = 0; i < block.size(); ++i)
      CHECK(single->at(first + i) == block[i]);
  }
}

TEST_CASE("a rebuilt filter matches a replay from the very first innovation") {
  // Bounded history is only a shortcut if it lands on the same number the long
  // way round would. Brown noise is simple enough to replay here in full.
  constexpr double leak = 0.99, sigma = 1.0;
  constexpr std::uint64_t target = 120'000;
  auto brown = build(
      R"({"seed":5,"signal":{"type":"brown_noise","sigma":1.0,"leak":0.99}})");

  SigGen::Rng rng(5);
  const double drive = sigma * std::sqrt(1.0 - leak * leak);
  double state = 0.0;
  for (std::uint64_t i = 0; i <= target; ++i)
    state = leak * state + drive * rng.gaussian_at(i);

  CHECK(brown->at(target) == doctest::Approx(state).epsilon(1e-14));
}

TEST_CASE("the waveforms are addressed without any history at all") {
  auto sine = build(R"({"sample_rate":1000,"signal":{
      "type":"sine","frequency":50,"amplitude":2,"noiseless":true}})");
  // 50 Hz at 1 kHz: twenty samples to the period, so a quarter of that is the
  // peak, and the phase repeats exactly however far out the index is.
  CHECK(sine->at(0) == doctest::Approx(0.0));
  CHECK(sine->at(5) == doctest::Approx(2.0));
  CHECK(sine->at(10) == doctest::Approx(0.0));
  CHECK(sine->at(1'000'000'005ULL) == doctest::Approx(2.0));
}

TEST_CASE("every generator reports whether it can be addressed") {
  CHECK(build(R"({"signal":{"type":"sine","frequency":1}})")->is_addressable());
  CHECK(
      build(R"({"signal":{"type":"square","frequency":1}})")->is_addressable());
  CHECK(build(R"({"signal":{"type":"triangle","frequency":1}})")
            ->is_addressable());
  CHECK(build(R"({"signal":{"type":"sawtooth","frequency":1}})")
            ->is_addressable());
  CHECK(build(R"({"signal":{"type":"white_noise"}})")->is_addressable());
  CHECK(build(R"({"signal":{"type":"pink_noise"}})")->is_addressable());
  CHECK(build(R"({"signal":{"type":"brown_noise"}})")->is_addressable());
  CHECK(build(R"({"signal":{"type":"custom","points":[[0,0],[1,1]]}})")
            ->is_addressable());
  CHECK(build(R"({"signal":{"type":"arima","ar":[0.5]}})")->is_addressable());
}

TEST_CASE("integration cannot be addressed, and says so") {
  auto walk =
      build(R"({"signal":{"type":"arima","ar":[0.6],"d":1,"sigma":1}})");
  CHECK_FALSE(walk->is_addressable());
  CHECK_THROWS_AS(walk->at(10), SigGen::SigGenException);
  // The message has to explain why, not merely refuse.
  REQUIRE_THROWS_WITH_AS(walk->at(10), doctest::Contains("cumulative sum"),
                         SigGen::SigGenException);
}

TEST_CASE("a composite is addressable only if all of its components are") {
  auto mixed = build(R"({"signal":{"type":"composite","components":[
        {"type":"sine","frequency":5},
        {"type":"arima","ar":[0.5],"d":1}]}})");
  CHECK_FALSE(mixed->is_addressable());
  CHECK_THROWS_AS(mixed->at(0), SigGen::SigGenException);
}

TEST_CASE("a composite combines its components at an index") {
  auto sum = build(R"({"sample_rate":100,"signal":{
      "type":"composite","op":"sum","components":[
        {"type":"sine","frequency":5,"noiseless":true},
        {"type":"sine","frequency":5,"noiseless":true,"gain":2}]}})");
  auto one = build(R"({"sample_rate":100,"signal":{
      "type":"sine","frequency":5,"noiseless":true}})");
  for (std::uint64_t i = 0; i < 40; ++i)
    CHECK(sum->at(i) == doctest::Approx(3.0 * one->at(i)));

  auto product = build(R"({"sample_rate":200,"signal":{
      "type":"composite","op":"multiply","components":[
        {"type":"sine","frequency":40,"noiseless":true},
        {"type":"sine","frequency":2,"noiseless":true}]}})");
  auto carrier = build(R"({"sample_rate":200,"signal":{
      "type":"sine","frequency":40,"noiseless":true}})");
  auto envelope = build(R"({"sample_rate":200,"signal":{
      "type":"sine","frequency":2,"noiseless":true}})");
  for (std::uint64_t i = 0; i < 40; ++i)
    CHECK(product->at(i) == doctest::Approx(carrier->at(i) * envelope->at(i)));
}

TEST_CASE("the noise floor draws apart from the innovations it sits on") {
  // Both the intrinsic noise and a stochastic generator's own innovations are
  // addressed by index. Were they to share a sub-stream, the "noise" added to
  // pink noise would be a multiple of the pink sample rather than an
  // independent perturbation, and the two would correlate perfectly.
  auto noisy = build(
      R"({"seed":21,"signal":{"type":"pink_noise","sigma":1,"snr_db":6}})");
  auto clean = build(
      R"({"seed":21,"signal":{"type":"pink_noise","sigma":1,"noiseless":true}})");

  const std::size_t n = 4000;
  const std::vector<double> with = noisy->values_at(0, n);
  const std::vector<double> without = clean->values_at(0, n);

  double cross = 0.0, added_power = 0.0, clean_power = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    const double added = with[i] - without[i];
    cross += added * without[i];
    added_power += added * added;
    clean_power += without[i] * without[i];
  }
  REQUIRE(added_power > 0.0);
  const double correlation = cross / std::sqrt(added_power * clean_power);
  CHECK(std::abs(correlation) < 0.1);
}

TEST_CASE("the epoch reaches a composite's components") {
  auto composite = SigGen::from_string(R"({"signal":{
      "type":"composite","components":[{"type":"sine","frequency":5}]}})");
  composite->set_epoch(epoch);
  CHECK(composite->epoch() == epoch);
  // Nothing observable unless it propagated: the child converts indices too.
  CHECK(composite->index_at(epoch) == 0);
}

TEST_CASE("a run of indices is timestamped from the epoch") {
  auto signal =
      build(R"({"sample_rate":50,"signal":{"type":"sine","frequency":1}})");
  const auto series = signal->at_range(100, 4);
  REQUIRE(series.size() == 4);
  CHECK(series[0].first == doctest::Approx(epoch + 2.0));
  CHECK(series[3].first == doctest::Approx(epoch + 2.06));
  CHECK(series[2].second == signal->at(102));
}
