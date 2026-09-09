// Downstream consumer that shares its own nlohmann_json and cxxopts with
// SigGen, exercising both libraries from either side of the boundary.
//
// Copyright 2026 Paolo Bosetti
// SPDX-License-Identifier: Apache-2.0

#include <siggen.hpp>

#include <cxxopts.hpp>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <iostream>

int main() {
  // The consumer's own nlohmann_json, used directly...
  nlohmann::json spec;
  spec["type"] = "sine";
  spec["frequency"] = 10.0;
  spec["noiseless"] = true;

  // ...and handed straight to SigGen, which must be compiled against the very
  // same copy for this to link, let alone work.
  auto signal = SigGen::from_json(spec);
  signal->set_sample_rate(40.0);
  const auto values = signal->take(4);
  if (values[1] < 0.99 || values[1] > 1.01) {
    std::cerr << "consumer-shared: unexpected sample " << values[1] << '\n';
    return 1;
  }

  // The consumer's own cxxopts, likewise shared with SigGen's tool.
  cxxopts::Options options("consumer_shared", "shared-dependency smoke test");
  options.add_options()("h,help", "Print usage");
  if (options.help().empty()) {
    std::cerr << "consumer-shared: cxxopts produced no help text\n";
    return 1;
  }

  std::printf("consumer-shared ok: y[1] = %.4f, json %d.%d.%d\n", values[1],
              NLOHMANN_JSON_VERSION_MAJOR, NLOHMANN_JSON_VERSION_MINOR,
              NLOHMANN_JSON_VERSION_PATCH);
  return 0;
}
