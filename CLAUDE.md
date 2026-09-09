# AIM

This is a portable C++ library that generates fake signals to be used for testing purposes.

# Requirements

* C++20 compiler
* CMake 3.20 or higher
* FetchContent consumable
* portable on macOS, Linux, Windows (Visual Studio compiler)
* No external dependencies, except nlohmann/json and pbosetti/expressionist (github handles)
* Header-only library
* Simple API with sensible defaults
* Companion, lightweight, command-line tool to generate signals from the command line, with the option to display them as time series using ASCII art (namely, using 6-dots characters a la btop)
* signals:
  * sinusoidal
  * square
  * triangular
  * sawtooth
  * composition of the above
  * white noise
  * pink noise
  * brown noise
  * custom signals (user-defined as list of time-values pairs)
  * ARIMA (AutoRegressive Integrated Moving Average) signals
* signal defaults always compose a white noise element to avoid generating a perfect signal, which is unrealistic in real-world scenarios
 * signal configuration can also be done by providing a JSON file, which is parsed using nlohmann/json library with the pbosetti/expressionist library to evaluate mathematical expressions for the signal parameters.