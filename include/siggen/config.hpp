// SigGen -- building signals from JSON configuration.
//
// Copyright 2026 Paolo Bosetti
// SPDX-License-Identifier: Apache-2.0
//
// The document is first handed to Expressionist, so any numeric field may be
// written as an algebraic expression over the document's own keys: a harmonic
// can be declared as "$3 * f0" instead of a magic number, and a table of knot
// times as a range. Only after that substitution is the tree interpreted as a
// signal description.

#pragma once

#include "arima.hpp"
#include "composite.hpp"
#include "custom.hpp"
#include "noise.hpp"
#include "periodic.hpp"
#include "signal.hpp"

#include <expressionist.hpp>
#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace SigGen {

namespace detail {

using json = nlohmann::json;

inline std::string lower(std::string s) {
  for (char &c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

/// Prefix every diagnostic with the JSON pointer of the offending node, the
/// way ExpressionistException reports its own errors.
[[noreturn]] inline void fail(const std::string &path,
                              const std::string &message) {
  throw SigGenException("In '" + (path.empty() ? "/" : path) + "': " + message);
}

inline double number(const json &node, const std::string &path, const char *key,
                     double fallback) {
  const auto it = node.find(key);
  if (it == node.end() || it->is_null())
    return fallback;
  if (!it->is_number())
    fail(path + "/" + key,
         "expected a number, got " + std::string(it->type_name()));
  return it->get<double>();
}

inline double required_number(const json &node, const std::string &path,
                              const char *key) {
  const auto it = node.find(key);
  if (it == node.end())
    fail(path, std::string("missing required key '") + key + "'");
  if (!it->is_number())
    fail(path + "/" + key,
         "expected a number, got " + std::string(it->type_name()));
  return it->get<double>();
}

inline std::vector<double>
number_array(const json &node, const std::string &path, const char *key) {
  const auto it = node.find(key);
  if (it == node.end() || it->is_null())
    return {};
  if (!it->is_array())
    fail(path + "/" + key,
         "expected an array, got " + std::string(it->type_name()));
  std::vector<double> out;
  out.reserve(it->size());
  for (std::size_t i = 0; i < it->size(); ++i) {
    const json &element = (*it)[i];
    if (!element.is_number())
      fail(path + "/" + key + "/" + std::to_string(i),
           "expected a number, got " + std::string(element.type_name()));
    out.push_back(element.get<double>());
  }
  return out;
}

inline std::string keyword(const json &node, const std::string &path,
                           const char *key, const std::string &fallback) {
  const auto it = node.find(key);
  if (it == node.end() || it->is_null())
    return fallback;
  if (!it->is_string())
    fail(path + "/" + key,
         "expected a string, got " + std::string(it->type_name()));
  return lower(it->get<std::string>());
}

/// Apply the noise policy shared by every generator: an explicit "snr_db"
/// wins, "noiseless": true switches the intrinsic noise off, and a null
/// "snr_db" does the same. Otherwise the type's own default stands.
inline void apply_noise(Signal &signal, const json &node,
                        const std::string &path) {
  const auto noiseless = node.find("noiseless");
  if (noiseless != node.end()) {
    if (!noiseless->is_boolean())
      fail(path + "/noiseless",
           "expected a boolean, got " + std::string(noiseless->type_name()));
    if (noiseless->get<bool>()) {
      signal.set_noiseless();
      return;
    }
  }
  const auto snr = node.find("snr_db");
  if (snr == node.end())
    return;
  if (snr->is_null()) {
    signal.set_noiseless();
    return;
  }
  if (!snr->is_number())
    fail(path + "/snr_db",
         "expected a number or null, got " + std::string(snr->type_name()));
  signal.set_snr_db(snr->get<double>());
}

inline Custom::Interp parse_interp(const std::string &name,
                                   const std::string &path) {
  if (name == "linear")
    return Custom::Interp::LINEAR;
  if (name == "step")
    return Custom::Interp::STEP;
  if (name == "cubic")
    return Custom::Interp::CUBIC;
  fail(path + "/interpolation",
       "unknown interpolation '" + name + "'; expected linear, step or cubic");
}

inline Custom::Extrap parse_extrap(const std::string &name,
                                   const std::string &path) {
  if (name == "repeat")
    return Custom::Extrap::REPEAT;
  if (name == "clamp")
    return Custom::Extrap::CLAMP;
  if (name == "zero")
    return Custom::Extrap::ZERO;
  fail(path + "/extrapolation",
       "unknown extrapolation '" + name + "'; expected repeat, clamp or zero");
}

/// Read the knot table, accepting either an array of [time, value] pairs or a
/// pair of parallel "t" and "y" arrays -- the latter pairs naturally with
/// Expressionist's range operator, e.g. "t": "$0:1:0.1".
inline std::vector<Custom::Point> parse_points(const json &node,
                                               const std::string &path) {
  if (node.contains("t") || node.contains("y")) {
    const std::vector<double> t = number_array(node, path, "t");
    const std::vector<double> y = number_array(node, path, "y");
    if (t.size() != y.size())
      fail(path, "'t' and 'y' must have the same length, got " +
                     std::to_string(t.size()) + " and " +
                     std::to_string(y.size()));
    std::vector<Custom::Point> points;
    points.reserve(t.size());
    for (std::size_t i = 0; i < t.size(); ++i)
      points.emplace_back(t[i], y[i]);
    return points;
  }

  const auto it = node.find("points");
  if (it == node.end())
    fail(path, "a custom signal needs 'points', or parallel 't' and 'y'");
  if (!it->is_array())
    fail(path + "/points",
         "expected an array, got " + std::string(it->type_name()));
  std::vector<Custom::Point> points;
  points.reserve(it->size());
  for (std::size_t i = 0; i < it->size(); ++i) {
    const json &pair = (*it)[i];
    const std::string where = path + "/points/" + std::to_string(i);
    if (!pair.is_array() || pair.size() != 2 || !pair[0].is_number() ||
        !pair[1].is_number())
      fail(where, "expected a [time, value] pair of numbers");
    points.emplace_back(pair[0].get<double>(), pair[1].get<double>());
  }
  return points;
}

std::unique_ptr<Signal> build(const json &node, const std::string &path);

inline std::unique_ptr<Signal> build_composite(const json &node,
                                               const std::string &path) {
  const std::string op = keyword(node, path, "op", "sum");
  Composite::Op mode = Composite::Op::SUM;
  if (op == "multiply" || op == "product")
    mode = Composite::Op::MULTIPLY;
  else if (op != "sum" && op != "add")
    fail(path + "/op",
         "unknown operator '" + op + "'; expected sum or multiply");

  auto composite = std::make_unique<Composite>(mode);
  const auto it = node.find("components");
  if (it == node.end())
    fail(path, "a composite signal needs 'components'");
  if (!it->is_array())
    fail(path + "/components",
         "expected an array, got " + std::string(it->type_name()));
  for (std::size_t i = 0; i < it->size(); ++i) {
    const std::string where = path + "/components/" + std::to_string(i);
    const json &child = (*it)[i];
    composite->add(build(child, where), number(child, where, "gain", 1.0));
  }
  return composite;
}

/// Turn one already-evaluated node into a signal.
inline std::unique_ptr<Signal> build(const json &node,
                                     const std::string &path) {
  if (!node.is_object())
    fail(path,
         "expected a signal object, got " + std::string(node.type_name()));
  const auto type_it = node.find("type");
  if (type_it == node.end())
    fail(path, "missing required key 'type'");
  if (!type_it->is_string())
    fail(path + "/type",
         "expected a string, got " + std::string(type_it->type_name()));
  const std::string type = lower(type_it->get<std::string>());

  std::unique_ptr<Signal> signal;
  if (type == "sine") {
    signal = std::make_unique<Sine>(required_number(node, path, "frequency"),
                                    number(node, path, "amplitude", 1.0),
                                    number(node, path, "phase", 0.0),
                                    number(node, path, "offset", 0.0));
  } else if (type == "square") {
    signal = std::make_unique<Square>(
        required_number(node, path, "frequency"),
        number(node, path, "amplitude", 1.0), number(node, path, "duty", 0.5),
        number(node, path, "phase", 0.0), number(node, path, "offset", 0.0));
  } else if (type == "triangle") {
    signal = std::make_unique<Triangle>(
        required_number(node, path, "frequency"),
        number(node, path, "amplitude", 1.0),
        number(node, path, "symmetry", 0.5), number(node, path, "phase", 0.0),
        number(node, path, "offset", 0.0));
  } else if (type == "sawtooth") {
    signal = std::make_unique<Sawtooth>(
        required_number(node, path, "frequency"),
        number(node, path, "amplitude", 1.0), number(node, path, "phase", 0.0),
        number(node, path, "offset", 0.0));
  } else if (type == "white_noise" || type == "white") {
    signal = std::make_unique<WhiteNoise>(number(node, path, "sigma", 1.0));
  } else if (type == "pink_noise" || type == "pink") {
    signal = std::make_unique<PinkNoise>(number(node, path, "sigma", 1.0));
  } else if (type == "brown_noise" || type == "brown") {
    signal = std::make_unique<BrownNoise>(number(node, path, "sigma", 1.0),
                                          number(node, path, "leak", 0.99));
  } else if (type == "custom") {
    signal = std::make_unique<Custom>(
        parse_points(node, path),
        parse_interp(keyword(node, path, "interpolation", "linear"), path),
        parse_extrap(keyword(node, path, "extrapolation", "repeat"), path));
  } else if (type == "arima") {
    const double order = number(node, path, "d", 0.0);
    if (order < 0.0 || order != std::floor(order))
      fail(path + "/d", "the order of integration must be a non-negative "
                        "integer, got " +
                            std::to_string(order));
    signal = std::make_unique<Arima>(
        number_array(node, path, "ar"), static_cast<std::size_t>(order),
        number_array(node, path, "ma"), number(node, path, "sigma", 1.0));
  } else if (type == "composite") {
    signal = build_composite(node, path);
  } else {
    fail(path + "/type", "unknown signal type '" + type + "'");
  }

  apply_noise(*signal, node, path);
  return signal;
}

/// Substitute every algebraic expression in the document.
inline json evaluate(const json &document) {
  try {
    Expressionist::Expressionist engine;
    return engine.produce(document);
  } catch (const Expressionist::ExpressionistException &e) {
    throw SigGenException(std::string("expression error: ") + e.what());
  }
}

} // namespace detail

/// Build a signal from a JSON document.
///
/// The document is either a bare signal object (carrying a "type" key) or a
/// wrapper holding the signal under "signal" plus the stream-level settings
/// "sample_rate" and "seed". Algebraic expressions are evaluated first, so any
/// numeric field may be written as one.
inline std::unique_ptr<Signal> from_json(const nlohmann::json &document) {
  const nlohmann::json evaluated = detail::evaluate(document);
  if (!evaluated.is_object())
    detail::fail("", "expected a JSON object, got " +
                         std::string(evaluated.type_name()));

  const bool wrapped =
      !evaluated.contains("type") && evaluated.contains("signal");
  std::unique_ptr<Signal> signal =
      wrapped ? detail::build(evaluated.at("signal"), "/signal")
              : detail::build(evaluated, "");

  if (const auto it = evaluated.find("sample_rate"); it != evaluated.end()) {
    if (!it->is_number())
      detail::fail("/sample_rate",
                   "expected a number, got " + std::string(it->type_name()));
    signal->set_sample_rate(it->get<double>());
  }
  if (const auto it = evaluated.find("seed"); it != evaluated.end()) {
    if (!it->is_number_unsigned())
      detail::fail("/seed", "expected a non-negative integer, got " +
                                std::string(it->type_name()));
    signal->set_seed(it->get<std::uint64_t>());
  }
  return signal;
}

/// Build a signal from JSON text.
inline std::unique_ptr<Signal> from_string(const std::string &text) {
  try {
    return from_json(nlohmann::json::parse(text));
  } catch (const nlohmann::json::parse_error &e) {
    throw SigGenException(std::string("JSON parse error: ") + e.what());
  }
}

/// Build a signal from a JSON file.
inline std::unique_ptr<Signal> from_file(const std::filesystem::path &path) {
  std::ifstream in(path);
  if (!in)
    throw SigGenException("cannot open '" + path.string() + "'");
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return from_string(buffer.str());
}

} // namespace SigGen
