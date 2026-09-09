// SigGen example -- a short tour of the library.
//
// Copyright 2026 Paolo Bosetti
// SPDX-License-Identifier: Apache-2.0

#include <siggen.hpp>

#include <iostream>

int main() {
  using namespace SigGen;

  // A signal is a stream: next() yields one sample and advances the clock.
  Sine sine(50.0, 1.0);
  sine.set_sample_rate(1000.0);
  std::cout << "A 50 Hz sine, sampled at 1 kHz, first five samples:\n";
  for (int i = 0; i < 5; ++i)
    std::cout << "  t = " << sine.time() << " s\ty = " << sine.next() << '\n';

  // Every deterministic waveform carries a white-noise floor by default, so
  // nothing ever comes out unrealistically perfect.
  std::cout << "\nIts intrinsic noise sits " << *sine.snr_db()
            << " dB down, i.e. sigma = " << sine.noise_sigma() << ".\n";

  // Composition: a carrier, a third harmonic, some drift and extra noise.
  Composite mix;
  mix.add(Sine(5.0, 1.0));
  mix.add(Sine(15.0, 0.3));
  mix.add(BrownNoise(0.08));
  mix.set_sample_rate(1000.0);
  mix.set_seed(42);

  std::cout << "\nCarrier plus third harmonic plus drift:\n";
  std::cout << plot(mix.take_series(600), {.width = 76, .height = 12});

  // Multiplication modulates one signal by another.
  Composite modulated(Composite::Op::MULTIPLY);
  modulated.add(Sine(40.0, 1.0));
  modulated.add(Sine(2.0, 1.0));
  modulated.set_sample_rate(1000.0);

  std::cout << "\nA 40 Hz carrier under a 2 Hz envelope:\n";
  std::cout << plot(modulated.take_series(500), {.width = 76, .height = 12});

  // The same thing declared as JSON, with algebraic expressions for the
  // parameters: `f0` is defined once and the harmonic derived from it.
  const auto configured = from_string(R"({
    "sample_rate": 500,
    "seed": 7,
    "f0": 3,
    "signal": {
      "type": "composite",
      "components": [
        {"type": "sawtooth", "frequency": "$f0", "amplitude": 1},
        {"type": "sine", "frequency": "$4 * f0", "amplitude": 0.15}
      ]
    }
  })");

  std::cout << "\nA sawtooth and its fourth harmonic, from JSON:\n";
  std::cout << plot(configured->take_series(400), {.width = 76, .height = 12});

  // An ARIMA(1,1,0) process: a drifting series of the kind a slow sensor
  // produces.
  Arima drift({0.6}, 1, {}, 0.05);
  drift.set_sample_rate(100.0);
  drift.set_seed(3);
  std::cout << "\nAn ARIMA(1,1,0) drift:\n";
  std::cout << plot(drift.take_series(400), {.width = 76, .height = 12});

  return 0;
}
