// siggen -- command-line front-end for the SigGen library.
//
// Copyright 2026 Paolo Bosetti
// SPDX-License-Identifier: Apache-2.0
//
// Generates a signal, either from a JSON configuration or from command-line
// flags, and writes it out as delimited text, as JSON, or as a braille plot.

#include <siggen.hpp>

#include <cxxopts.hpp>

#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

#ifndef SIGGEN_VERSION
#define SIGGEN_VERSION "unknown"
#endif

namespace {

constexpr int exit_ok = 0;
constexpr int exit_error = 1;
constexpr int exit_usage = 2;

std::string read_stream(std::istream &in) {
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

/// Copy a flag into the JSON spec only when the user actually gave it, so that
/// each signal type keeps its own defaults for everything left unsaid.
void set_if_present(nlohmann::json &spec, const cxxopts::ParseResult &options,
                    const char *flag, const char *key) {
  if (options.count(flag) > 0)
    spec[key] = options[flag].as<double>();
}

/// Assemble a signal description from the individual flags. Going through the
/// same JSON front-end the configuration files use keeps one set of defaults
/// and one set of diagnostics rather than two.
nlohmann::json spec_from_flags(const cxxopts::ParseResult &options) {
  nlohmann::json spec;
  spec["type"] = options["type"].as<std::string>();
  set_if_present(spec, options, "frequency", "frequency");
  set_if_present(spec, options, "amplitude", "amplitude");
  set_if_present(spec, options, "phase", "phase");
  set_if_present(spec, options, "offset", "offset");
  set_if_present(spec, options, "duty", "duty");
  set_if_present(spec, options, "symmetry", "symmetry");
  set_if_present(spec, options, "sigma", "sigma");
  if (options["noiseless"].as<bool>())
    spec["noiseless"] = true;
  else
    set_if_present(spec, options, "snr", "snr_db");
  return spec;
}

void write_delimited(std::ostream &out,
                     const std::vector<std::pair<double, double>> &series,
                     char separator) {
  out << "t" << separator << "y" << '\n';
  char buffer[64];
  for (const auto &[t, y] : series) {
    std::snprintf(buffer, sizeof(buffer), "%.10g", t);
    out << buffer << separator;
    std::snprintf(buffer, sizeof(buffer), "%.10g", y);
    out << buffer << '\n';
  }
}

void write_json(std::ostream &out,
                const std::vector<std::pair<double, double>> &series,
                double sample_rate) {
  nlohmann::json document;
  document["sample_rate"] = sample_rate;
  for (const auto &[t, y] : series) {
    document["t"].push_back(t);
    document["y"].push_back(y);
  }
  out << document.dump(2) << '\n';
}

} // namespace

int main(int argc, char **argv) {
#if defined(_WIN32)
  // Braille cells are emitted as UTF-8 bytes; without this the Windows console
  // renders them as mojibake in its default code page.
  SetConsoleOutputCP(CP_UTF8);
#endif

  cxxopts::Options options(
      "siggen",
      "Generate synthetic test signals.\n"
      "The signal comes from --config, from a positional JSON argument, from "
      "--type plus its parameters, or from JSON on stdin.");

  // clang-format off
  options.add_options("Signal")
      ("c,config", "Read the signal description from a JSON file",
          cxxopts::value<std::string>())
      ("type", "Signal type: sine, square, triangle, sawtooth, white_noise, "
               "pink_noise, brown_noise, custom, arima or composite",
          cxxopts::value<std::string>()->default_value("sine"))
      ("frequency", "Frequency in hertz", cxxopts::value<double>())
      ("amplitude", "Peak amplitude", cxxopts::value<double>())
      ("phase", "Initial phase in radians", cxxopts::value<double>())
      ("offset", "Constant added to every sample", cxxopts::value<double>())
      ("duty", "Duty cycle of a square wave, in (0, 1)", cxxopts::value<double>())
      ("symmetry", "Rising fraction of a triangle wave", cxxopts::value<double>())
      ("sigma", "Standard deviation of a noise signal", cxxopts::value<double>())
      ("snr", "Signal-to-noise ratio in dB of the intrinsic white noise",
          cxxopts::value<double>())
      ("noiseless", "Switch the intrinsic white noise off",
          cxxopts::value<bool>()->default_value("false"));

  options.add_options("Sampling")
      ("r,rate", "Sampling frequency in hertz", cxxopts::value<double>())
      ("n,samples", "Number of samples to generate",
          cxxopts::value<std::size_t>())
      ("d,duration", "Duration in seconds, as an alternative to --samples",
          cxxopts::value<double>())
      ("s,seed", "Seed of the random stream", cxxopts::value<std::uint64_t>());

  options.add_options("Output")
      ("f,format", "Output format: csv, tsv or json",
          cxxopts::value<std::string>()->default_value("csv"))
      ("o,output", "Write to this file instead of stdout",
          cxxopts::value<std::string>())
      ("p,plot", "Draw the signal as a braille plot instead of listing it",
          cxxopts::value<bool>()->default_value("false"))
      ("W,width", "Plot width in characters",
          cxxopts::value<std::size_t>()->default_value("80"))
      ("H,height", "Plot height in character rows",
          cxxopts::value<std::size_t>()->default_value("15"))
      ("ascii", "Plot with ASCII marks instead of braille dots",
          cxxopts::value<bool>()->default_value("false"))
      ("input", "Signal description as JSON (positional)",
          cxxopts::value<std::string>())
      ("h,help", "Print usage and exit")
      ("V,version", "Print version and exit");
  // clang-format on
  options.parse_positional({"input"});
  options.positional_help("[JSON]");

  try {
    const cxxopts::ParseResult parsed = options.parse(argc, argv);

    if (parsed.count("help") > 0) {
      std::cout << options.help() << '\n';
      return exit_ok;
    }
    if (parsed.count("version") > 0) {
      std::cout << "siggen " << SIGGEN_VERSION << '\n';
      return exit_ok;
    }

    const std::string format = parsed["format"].as<std::string>();
    if (format != "csv" && format != "tsv" && format != "json") {
      std::cerr << "siggen: unknown format '" << format
                << "'; expected csv, tsv or json\n";
      return exit_usage;
    }

    std::unique_ptr<SigGen::Signal> signal;
    if (parsed.count("config") > 0) {
      signal = SigGen::from_file(parsed["config"].as<std::string>());
    } else if (parsed.count("input") > 0) {
      signal = SigGen::from_string(parsed["input"].as<std::string>());
    } else if (parsed.count("type") > 0) {
      signal = SigGen::from_json(spec_from_flags(parsed));
    } else {
      const std::string text = read_stream(std::cin);
      if (text.find_first_not_of(" \t\r\n") == std::string::npos) {
        std::cerr << "siggen: no signal given; use --type, --config, a JSON "
                     "argument or stdin\n";
        return exit_usage;
      }
      signal = SigGen::from_string(text);
    }

    // Command-line sampling flags override whatever the document asked for.
    if (parsed.count("rate") > 0)
      signal->set_sample_rate(parsed["rate"].as<double>());
    if (parsed.count("seed") > 0)
      signal->set_seed(parsed["seed"].as<std::uint64_t>());

    std::size_t count = 1000;
    if (parsed.count("samples") > 0) {
      count = parsed["samples"].as<std::size_t>();
    } else if (parsed.count("duration") > 0) {
      const double seconds = parsed["duration"].as<double>();
      if (seconds < 0.0) {
        std::cerr << "siggen: duration must not be negative\n";
        return exit_usage;
      }
      count = static_cast<std::size_t>(seconds * signal->sample_rate());
    }

    const auto series = signal->take_series(count);

    std::ofstream file;
    if (parsed.count("output") > 0) {
      file.open(parsed["output"].as<std::string>());
      if (!file) {
        std::cerr << "siggen: cannot write to '"
                  << parsed["output"].as<std::string>() << "'\n";
        return exit_error;
      }
    }
    std::ostream &out = file.is_open() ? file : std::cout;

    if (parsed["plot"].as<bool>()) {
      SigGen::PlotOptions plot_options;
      plot_options.width = parsed["width"].as<std::size_t>();
      plot_options.height = parsed["height"].as<std::size_t>();
      plot_options.ascii = parsed["ascii"].as<bool>();
      out << SigGen::plot(series, plot_options);
    } else if (format == "json") {
      write_json(out, series, signal->sample_rate());
    } else {
      write_delimited(out, series, format == "tsv" ? '\t' : ',');
    }
    return exit_ok;
  } catch (const cxxopts::exceptions::exception &e) {
    std::cerr << "siggen: " << e.what() << '\n';
    return exit_usage;
  } catch (const SigGen::SigGenException &e) {
    std::cerr << "siggen: " << e.what() << '\n';
    return exit_error;
  } catch (const std::exception &e) {
    std::cerr << "siggen: " << e.what() << '\n';
    return exit_error;
  }
}
