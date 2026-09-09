# SigGen

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Header-only](https://img.shields.io/badge/header--only-yes-brightgreen.svg)](include/siggen.hpp)
[![CI](https://github.com/pbosetti/SigGen/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/pbosetti/SigGen/actions/workflows/ci.yml)

A small, header-only **C++20** library that synthesises fake signals for
testing: periodic waveforms, coloured noise, ARIMA processes and user-supplied
tables, alone or composed. Signals can be described in code or in a JSON
document whose numeric fields may be algebraic expressions, evaluated by
[Expressionist](https://github.com/pbosetti/Expressionist). A companion
command-line tool prints them as CSV or draws them in the terminal with braille
art.

```cpp
#include <siggen.hpp>

SigGen::Composite mix;
mix.add(SigGen::Sine(50.0, 1.0));    // carrier
mix.add(SigGen::Sine(150.0, 0.3));   // third harmonic
mix.add(SigGen::BrownNoise(0.05));   // slow drift
mix.set_sample_rate(1000.0);

std::cout << SigGen::plot(mix.take_series(400));
```

Nothing comes out unrealistically clean: every deterministic waveform carries a
white-noise floor by default, 40 dB below its own RMS, because a perfect signal
is not what a real acquisition chain ever delivers.

## Contents

- <a href="#features">Features</a>
- <a href="#requirements">Requirements</a>
- <a href="#integration">Integration</a>
- <a href="#dependencies-and-reuse">Dependencies and reuse</a>
- <a href="#quick-start">Quick start</a>
- <a href="#the-signal-model">The signal model</a>
- <a href="#signal-types">Signal types</a>
- <a href="#composition">Composition</a>
- <a href="#the-noise-floor">The noise floor</a>
- <a href="#json-configuration">JSON configuration</a>
- <a href="#plotting">Plotting</a>
- <a href="#command-line-tool">Command-line tool</a>
- <a href="#reproducibility">Reproducibility</a>
- <a href="#c-abi-and-python-interface">C ABI and Python interface</a>
- <a href="#api-summary">API summary</a>
- <a href="#building-and-testing">Building and testing</a>
- <a href="#license">License</a>

---

## Features

- **Header-only**, with no dependencies beyond `nlohmann::json` and
  `Expressionist` — and those only for `<siggen/config.hpp>`.
- Sine, square, triangle and sawtooth waveforms, driven by a phase accumulator
  so the frequency can change mid-stream without a phase jump.
- White, pink and brown noise, each calibrated to a requested standard
  deviation rather than an arbitrary filter gain.
- ARIMA(p, d, q) processes with an analytic stationary variance.
- Custom signals from a table of time-value pairs, with three interpolation and
  three extrapolation modes.
- Composition by sum or by product, so both "carrier plus harmonics" and
  amplitude modulation are expressible, and composites nest.
- A white-noise floor on every waveform by default, specified as a
  signal-to-noise ratio so it tracks the amplitude.
- JSON configuration in which any numeric field may be an algebraic expression
  over the document's own keys.
- Braille-art plotting in the terminal, with a plain-ASCII fallback.
- **Reproducible across platforms**: the same seed yields bit-identical samples
  on libstdc++, libc++ and MSVC.
- Consumable via CMake `FetchContent`.

## Requirements

- A C++20 compiler (tested with Clang, AppleClang and MSVC).
- CMake ≥ 3.20, with Ninja recommended.
- `nlohmann::json` and `Expressionist`, both fetched automatically.

## Integration

```cmake
include(FetchContent)
FetchContent_Declare(
  SigGen
  GIT_REPOSITORY https://github.com/pbosetti/SigGen.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(SigGen)

target_link_libraries(your_target PRIVATE SigGen::SigGen)
```

Linking the target pulls in the include path and both dependencies, so
`#include <siggen.hpp>` just works.

The umbrella header brings in everything. To keep a translation unit free of
`nlohmann::json` and `Expressionist`, include only what you need — every header
under `<siggen/>` except `config.hpp` is dependency-free:

```cpp
#include <siggen/periodic.hpp>   // no JSON, no Expressionist
```

## Dependencies and reuse

SigGen needs `nlohmann_json` and `Expressionist`, plus `cxxopts` for the tool
and `doctest` for the tests. A project consuming SigGen is very likely to use
some of the same libraries, and must not end up with two copies of any of them:
duplicated sources at best, and a hard configure error at worst, since
nlohmann/json defines `nlohmann_json::nlohmann_json` unconditionally and would
collide with a copy already in scope.

**SigGen adopts whatever the enclosing project has already provided, however it
was provided, and fetches only what is genuinely missing.** All of these work,
and all leave exactly one copy:

```cmake
# The consumer fetches its own pin. FetchContent honours the first declaration
# of a name, so this version wins and SigGen uses it.
FetchContent_Declare(nlohmann_json URL .../v3.11.3/json.tar.xz)
FetchContent_MakeAvailable(nlohmann_json)
FetchContent_MakeAvailable(SigGen)
```

```cmake
# The consumer loads an installed copy. There is a target but no declaration
# for FetchContent to defer to, so SigGen adopts the target directly.
find_package(nlohmann_json REQUIRED)
FetchContent_MakeAvailable(SigGen)
```

```cmake
# The consumer vendors its own. Same again.
add_subdirectory(external/json)
FetchContent_MakeAvailable(SigGen)
```

Adoption is recorded as a FetchContent population, so a *nested* project asking
for the same dependency leaves it alone too — which matters here, because
Expressionist declares `nlohmann_json` itself.

The configure output says which way each dependency went:

```
-- SigGen: using nlohmann_json::nlohmann_json already provided by the enclosing project
-- SigGen: providing Expressionist via FetchContent
-- SigGen: using cxxopts::cxxopts already provided by the enclosing project
```

Adoption is by target name — `nlohmann_json::nlohmann_json`,
`Expressionist::Expressionist`, `cxxopts::cxxopts`, `doctest::doctest` — and
SigGen takes what it is given without checking the version, so a consumer
pinning something much older than the versions below is on its own. SigGen's
own pins apply only when nothing else has claimed the name.

Both arrangements are covered by [test/consumer/](test/consumer) and
[test/consumer-shared/](test/consumer-shared), which the CI builds on every
push.

## Quick start

```cpp
#include <siggen/periodic.hpp>
#include <iostream>

int main() {
  SigGen::Sine sine(50.0, 1.0);     // 50 Hz, unit amplitude
  sine.set_sample_rate(1000.0);

  // One sample at a time...
  for (int i = 0; i < 5; ++i)
    std::cout << sine.time() << '\t' << sine.next() << '\n';

  // ...or in bulk.
  std::vector<double> block = sine.take(1000);
  std::vector<double> second = sine.take_for(1.0);   // by duration
  auto series = sine.take_series(100);               // (time, value) pairs
}
```

## The signal model

A signal is a **stateful stream**, not a function of `t`. `next()` returns one
sample and advances an internal clock by one sampling period:

```cpp
double y = signal.next();
signal.time();          // seconds, of the sample next() will produce next
signal.index();         // sample counter
signal.reset();         // rewind the clock and the random stream
```

Periodic waveforms could equally have been evaluated at an arbitrary instant,
but ARIMA processes and coloured noise cannot: each of their samples depends on
the ones before it. The sequential model is the only one that covers every
generator uniformly, and it is what a test harness pulling samples as it goes
needs anyway.

Everything derives from `SigGen::Signal`, so signals are interchangeable:

```cpp
void feed(SigGen::Signal &source, std::size_t n) {
  for (double y : source.take(n))
    device.push(y);
}
```

## Signal types

### Periodic waveforms

| Type       | Parameters                                          | RMS            |
|------------|-----------------------------------------------------|----------------|
| `Sine`     | frequency, amplitude, phase, offset                 | `A / sqrt(2)`  |
| `Square`   | …plus `duty` in (0, 1)                              | `A`            |
| `Triangle` | …plus `symmetry` in [0, 1]                          | `A / sqrt(3)`  |
| `Sawtooth` | frequency, amplitude, phase, offset                 | `A / sqrt(3)`  |

Phase is in radians and offset is excluded from the RMS, so adding a DC level
does not change the noise floor. A negative frequency runs the waveform
backwards.

```cpp
SigGen::Square pulse(10.0, 5.0, /*duty*/ 0.2);
SigGen::Triangle ramp(1.0, 1.0, /*symmetry*/ 0.9);   // slow rise, fast fall
```

### Noise

```cpp
SigGen::WhiteNoise white(1.0);          // flat spectrum
SigGen::PinkNoise pink(1.0);            // -3 dB/octave
SigGen::BrownNoise brown(1.0, 0.99);    // -6 dB/octave, leak coefficient
```

All three are parameterised by the standard deviation of their **output**, so
`sigma` means the same thing whatever the colour. Pink noise uses Paul Kellet's
six-pole approximation, normalised by the closed-form variance of its impulse
response rather than by a hand-tuned constant. Brown noise is a *leaky*
integrator: a pure random walk would drift without bound and make `sigma`
meaningless, whereas the leak gives it a finite, exactly known variance while
staying 1/f² above the corner frequency.

Noise generators carry no additional noise floor of their own.

### Custom signals

```cpp
SigGen::Custom table(
    {{0.0, 0.0}, {0.5, 1.0}, {1.0, 0.0}},
    SigGen::Custom::Interp::CUBIC,
    SigGen::Custom::Extrap::REPEAT);
```

Interpolation is `LINEAR` (default), `STEP` or `CUBIC` — a Hermite spline with
central-difference tangents that respects unevenly spaced knots. Extrapolation
is `REPEAT` (default), `CLAMP` or `ZERO`. The table is resampled at the
signal's own rate, so a coarse table can drive a fast stream.

Under `REPEAT`, `start_time()`–`end_time()` is exactly one period, which makes
the last knot the wrap point rather than a sample in its own right: to loop
seamlessly, end the table with a knot repeating the first value.

### ARIMA

```cpp
SigGen::Arima process(/*ar*/ {0.6}, /*d*/ 1, /*ma*/ {0.3}, /*sigma*/ 0.05);
```

The stationary part follows the usual convention,

```
x[t] = sum_i phi[i] x[t-1-i] + e[t] + sum_j theta[j] e[t-1-j]
```

with `e ~ N(0, sigma²)`, and the result is accumulated `d` times. The recursion
is warmed up on construction and on every reset, so the first sample handed out
already comes from the stationary distribution.

`rms()` reports the standard deviation of the **differenced** process, computed
in closed form from the MA(∞) representation. An integrated process has no
finite RMS, and a non-stationary AR part reports infinity rather than a
silently truncated number.

## Composition

```cpp
SigGen::Composite mix;                 // Op::SUM by default
mix.add(SigGen::Sine(50.0, 1.0));
mix.add(SigGen::Sine(150.0, 1.0), /*gain*/ 0.3);
mix.set_sample_rate(1000.0);

SigGen::Composite am(SigGen::Composite::Op::MULTIPLY);
am.add(SigGen::Sine(1000.0));          // carrier
am.add(SigGen::Sine(2.0));             // envelope
```

A composite owns clones of its components, so a signal can be added and then
modified or destroyed freely. Sampling rate and seed propagate to the
components — each getting a distinct derived seed, so two identical noise
components add rather than doubling one stream — and composites nest.

Every component is stepped on every sample in both modes, so the components
stay in lockstep and switching the operator does not shift their phases.

## The noise floor

Every deterministic waveform mixes in Gaussian white noise at
`default_snr_db` (40 dB) below its own RMS:

```cpp
SigGen::Sine sine(50.0, 1.0);
sine.snr_db();          // 40
sine.noise_sigma();     // 0.00707 = (1/sqrt(2)) / 100

sine.set_snr_db(20.0);  // louder noise
sine.set_noiseless();   // none at all
```

Because the level is a *ratio*, it tracks the amplitude: a signal scaled by a
hundred gets a hundred times the noise, and its plot looks the same. Noise
generators, ARIMA processes and composites carry no intrinsic floor — the first
two are stochastic already, and stacking one more layer per level of nesting
would make a composite's noise depend on how it happened to be structured.

## JSON configuration

```cpp
#include <siggen/config.hpp>

auto signal = SigGen::from_file("config.json");
auto other  = SigGen::from_string(R"({"type": "sine", "frequency": 50})");
```

The document is either a bare signal object (carrying `type`) or a wrapper
holding it under `signal` alongside `sample_rate` and `seed`. It is first
passed through Expressionist, so **any** numeric field may be an algebraic
expression over the document's own keys:

```jsonc
{
  "sample_rate": 2000,
  "seed": 20260909,
  "f0": 50,

  "signal": {
    "type": "composite",
    "op": "sum",
    "components": [
      { "type": "sine",   "frequency": "$f0",     "amplitude": 1.0, "snr_db": 45 },
      { "type": "sine",   "frequency": "$3 * f0", "amplitude": 0.25, "gain": 0.5 },
      { "type": "square", "frequency": "$f0 / 10", "duty": 0.2, "noiseless": true },
      { "type": "brown_noise", "sigma": 0.05 }
    ]
  }
}
```

### Keys by type

| `type`                        | Keys                                              |
|-------------------------------|---------------------------------------------------|
| `sine`, `sawtooth`            | `frequency` (required), `amplitude`, `phase`, `offset` |
| `square`                      | …plus `duty`                                      |
| `triangle`                    | …plus `symmetry`                                  |
| `white_noise`, `pink_noise`   | `sigma`                                           |
| `brown_noise`                 | `sigma`, `leak`                                   |
| `custom`                      | `points` or `t`/`y`, `interpolation`, `extrapolation` |
| `arima`                       | `ar`, `d`, `ma`, `sigma`                          |
| `composite`                   | `op`, `components`                                |

Every type also accepts `snr_db` (a number, or `null` to switch the floor off),
`noiseless`, and — inside a composite's `components` — `gain`.

A custom signal takes either a list of pairs or two parallel arrays, the latter
pairing naturally with Expressionist's range operator:

```jsonc
{ "type": "custom", "points": [[0, 0], [1, 10], [2, 0]] }
{ "type": "custom", "t": "$0:1:0.25", "y": [0, 2, 5, 9, 14] }
```

Errors name the offending node, as Expressionist's own do:

```
In '/components/0': missing required key 'frequency'
In '/type': unknown signal type 'sinus'
```

## Plotting

```cpp
#include <siggen/plot.hpp>

std::cout << SigGen::plot(signal.take_series(400));
std::cout << SigGen::plot(values, /*time_step*/ 0.001);
```

Each braille cell packs 2×4 dots, so an 80×15 plot has an effective resolution
of 160×60. `PlotOptions` controls `width`, `height`, `ascii`, `axes`, a forced
`y_min`/`y_max`, and the axis gutter. Columns spanning many samples show the
full excursion rather than one arbitrary value, and columns with no sample of
their own are interpolated, so the trace stays connected either way.

```
  1.009 |    *****                    ******
        |  ***   ***                 **    **
        | **       **               **      **
        |**         **             **        **
      0 |*           **           **          **           *
        |             **        ***            ***        **
        |              **      **                **      **
        |               ***   **                  **   ***
 -1.006 |                 *****                    *****
        +---------------------------------------------------
         0                                           0.995 s
```

The third y-label lands on the zero line whenever zero is on the canvas, and on
the midpoint otherwise.

## Command-line tool

The `siggen` executable is built by default when SigGen is the top-level
project; set `SIGGEN_BUILD_TOOL=OFF` to skip it and its `cxxopts` dependency.
The signal comes from `--config`, from a positional JSON argument, from
`--type` plus its parameters, or from JSON on stdin.

```sh
# From flags
siggen --type sine --frequency 50 --rate 1000 --samples 500

# From a configuration file, drawn in the terminal
siggen --config example/config.json --plot

# From inline JSON, as tab-separated values
siggen '{"type":"square","frequency":10,"duty":0.25}' --format tsv -n 100

# From stdin
echo '{"type":"pink_noise","sigma":2}' | siggen --plot --ascii
```

| Option                | Description                                          | Default |
|-----------------------|------------------------------------------------------|---------|
| `-c, --config arg`    | Read the signal description from a JSON file         |         |
| `--type arg`          | Signal type when building from flags                 | `sine`  |
| `--frequency arg`     | Frequency in hertz                                   |         |
| `--amplitude arg`     | Peak amplitude                                       |         |
| `--phase`, `--offset` | Initial phase in radians, constant offset            |         |
| `--duty`, `--symmetry`| Square duty cycle, triangle rising fraction          |         |
| `--sigma arg`         | Standard deviation of a noise signal                 |         |
| `--snr arg`           | Signal-to-noise ratio in dB of the intrinsic noise   | `40`    |
| `--noiseless`         | Switch the intrinsic noise off                       | off     |
| `-r, --rate arg`      | Sampling frequency in hertz                          | `1000`  |
| `-n, --samples arg`   | Number of samples                                    | `1000`  |
| `-d, --duration arg`  | Duration in seconds, instead of `--samples`          |         |
| `-s, --seed arg`      | Seed of the random stream                            |         |
| `-f, --format arg`    | `csv`, `tsv` or `json`                               | `csv`   |
| `-o, --output arg`    | Write to a file instead of stdout                    |         |
| `-p, --plot`          | Draw a braille plot instead of listing the samples   | off     |
| `-W, --width arg`     | Plot width in characters                             | `80`    |
| `-H, --height arg`    | Plot height in character rows                        | `15`    |
| `--ascii`             | Plot with ASCII marks instead of braille dots        | off     |
| `-h, --help`          | Print usage and exit                                 |         |
| `-V, --version`       | Print version and exit                               |         |

**Exit codes:** `0` on success, `1` on a runtime error (a bad configuration, an
unwritable output file), and `2` on a usage error.

## Reproducibility

A testing library that cannot reproduce a failure is not much use, so every
signal is deterministic by default and seeded explicitly when needed:

```cpp
SigGen::WhiteNoise noise(1.0);
noise.set_seed(42);
auto first = noise.take(1000);
noise.set_seed(42);
assert(noise.take(1000) == first);     // and reset() does the same
```

`<random>`'s distribution classes are free to draw differently on each standard
library, so SigGen derives its uniforms from the raw Mersenne Twister output
and its normals from the polar Box-Muller transform. A given seed therefore
produces **the same samples on every platform**, which the test suite pins down
with golden values.

Those same golden values are asserted from both sides of the FFI boundary --
[test/test_golden.cpp](test/test_golden.cpp) and
[test/python/test_c_api.py](test/python/test_c_api.py) carry the same literals
-- so a stream pulled through the C ABI or the Python wrapper is bit for bit
the stream the C++ API produces.

Reseeding implies a reset: pink noise, brown noise and ARIMA warm their state
up from the random stream, and keeping that state across a reseed would leave
it derived from the old seed.

## C ABI and Python interface

For use from outside C++, an optional `extern "C"` layer (`siggen_c.h` /
`libsiggen_c`) exposes the library through an opaque handle, so it is
consumable from any language with a C FFI. A `ctypes`-based Python package
wraps it.

Both are off by default, and the C++ library itself stays header-only:

```sh
cmake -Bbuild -GNinja -DSIGGEN_BUILD_PYTHON=ON
cmake --build build
cmake --install build
```

`SIGGEN_BUILD_PYTHON` implies `SIGGEN_BUILD_C_API` and, on install, copies
`libsiggen_c` and the `siggen` Python package into `Python3_SITELIB` — the
site-packages of whichever `python3` is first on `PATH`, so it lands in an
active virtualenv automatically. `import siggen` then just works, with no
`PYTHONPATH` or `LD_LIBRARY_PATH` setup.

A signal is described by the same JSON document the C++ `from_json()` and the
`siggen` tool accept, so every waveform, noise colour, ARIMA process, table and
composite is reachable through the one constructor — including the algebraic
expressions:

```python
import siggen

with siggen.Signal({"type": "sine", "frequency": 50, "snr_db": 25},
                   sample_rate=1000, seed=7) as s:
    samples = s.take(1000)          # numpy array, or a list without numpy
    print(s.type, s.rms, s.snr_db)
    print(s.plot(200, width=70))    # braille, as on the command line
```

`take()` fills a buffer on the C side and hands it back without a copy; pass
`out=` to write into a numpy array you already own. numpy is optional — without
it `take()` returns a plain list and everything else is unchanged.

**Generate in bulk.** `next()` exists, but one FFI call per sample costs far
more than generating the sample does — through ctypes, on the order of a
hundred times more. Reach for `take(n)` and let the loop run on the C side.

Unlike the C++ class, a handle is not thread-safe: a signal carries its stream
position, so one handle belongs to one thread at a time.

Errors surface as `SigGenError`, carrying the same diagnostic text the C++ API
produces:

```python
>>> siggen.Signal({"type": "no_such_signal"})
SigGenError: In '/type': unknown signal type 'no_such_signal'
>>> siggen.Signal({"type": "square", "frequency": 1, "duty": 5})
SigGenError: duty must lie in (0, 1), got 5.000000
```

To build just the shared library and header for a non-Python FFI consumer, use
`-DSIGGEN_BUILD_C_API=ON` instead. If you would rather point the wrapper at a
shared object of your own — a build-tree artefact, a custom install location —
set `SIGGEN_C_LIBRARY` to its path; it takes precedence over the
next-to-`__init__.py` lookup and the system library search.

The streams reached through the C ABI are the same ones the C++ API produces,
bit for bit; see <a href="#reproducibility">Reproducibility</a>.

## API summary

| Member                                   | Purpose                                     |
|------------------------------------------|---------------------------------------------|
| `double next()`                          | One sample, advancing the clock.            |
| `take(n)` / `take_series(n)` / `take_for(s)` | Bulk generation, by count or duration.  |
| `void reset()`                           | Rewind the clock and the random stream.     |
| `set_sample_rate(fs)` / `sample_rate()`  | Sampling frequency in hertz.                |
| `time()` / `index()` / `time_step()`     | Stream position.                            |
| `set_seed(s)` / `seed()`                 | Seed the random stream; implies a reset.    |
| `set_snr_db(x)` / `snr_db()` / `set_noiseless()` | The intrinsic noise floor.          |
| `double noise_sigma()`                   | Standard deviation of that floor.           |
| `double rms()`                           | Analytic RMS of the clean waveform.         |
| `std::string type()`                     | The `type` key of the JSON schema.          |
| `std::unique_ptr<Signal> clone()`        | A polymorphic copy.                         |
| `from_json` / `from_string` / `from_file`| Build a signal from a JSON document.        |
| `plot(series, options)`                  | Render a signal as ASCII art.               |

All failures throw `SigGen::SigGenException`, whose `what()` names the offending
node.

## Building and testing

```sh
cmake -Bbuild -GNinja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/siggen_example
```

| Option                  | Default        | Effect                          |
|-------------------------|----------------|---------------------------------|
| `SIGGEN_BUILD_TESTS`    | top-level only | Build the doctest suite.        |
| `SIGGEN_BUILD_TOOL`     | top-level only | Build the `siggen` executable.  |
| `SIGGEN_BUILD_EXAMPLES` | top-level only | Build the example program.      |
| `SIGGEN_BUILD_C_API`    | `OFF`          | Build the `siggen_c` shared library. |
| `SIGGEN_BUILD_PYTHON`   | `OFF`          | Build and install the Python wrapper (implies `SIGGEN_BUILD_C_API`). |

All three default to `ON` for a top-level build and `OFF` for a subproject, so
a consumer gets nothing but the interface library — but it can still ask, with
`-DSIGGEN_BUILD_TOOL=ON` or `set(SIGGEN_BUILD_TOOL ON CACHE BOOL "" FORCE)`
before `FetchContent_MakeAvailable(SigGen)`.

## License

Apache-2.0. See [LICENSE](LICENSE).
