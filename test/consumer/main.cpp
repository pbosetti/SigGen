// Downstream consumer smoke test: everything a user of the library touches,
// reached only through the SigGen::SigGen target.
//
// Copyright 2026 Paolo Bosetti
// SPDX-License-Identifier: Apache-2.0

#include <siggen.hpp>

#include <cstdio>
#include <iostream>

int main() {
  // The umbrella header must pull in the generators, the JSON front-end and
  // the plotter, with nlohmann/json and Expressionist reaching us transitively.
  auto signal = SigGen::from_string(R"({
    "sample_rate": 500,
    "seed": 1,
    "f0": 5,
    "signal": {
      "type": "composite",
      "components": [
        {"type": "sine", "frequency": "$f0"},
        {"type": "pink_noise", "sigma": 0.05}
      ]
    }
  })");

  const auto series = signal->take_series(200);
  if (series.size() != 200) {
    std::cerr << "consumer: wrong sample count\n";
    return 1;
  }
  std::cout << SigGen::plot(series, {.width = 60, .height = 8});

  SigGen::Composite mix(SigGen::Composite::Op::MULTIPLY);
  mix.add(SigGen::Sine(40.0));
  mix.add(SigGen::Triangle(2.0));
  mix.set_sample_rate(1000.0);
  std::printf("mix rms = %.4f over %zu samples\n", mix.rms(),
              mix.take(100).size());
  return 0;
}
