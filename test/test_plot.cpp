// The terminal plotter: geometry, glyph selection and degenerate input.
//
// Copyright 2026 Paolo Bosetti
// SPDX-License-Identifier: Apache-2.0

#include "test_common.hpp"

#include <doctest/doctest.h>
#include <siggen/periodic.hpp>
#include <siggen/plot.hpp>

#include <algorithm>
#include <string>

using namespace SigGen;

namespace {

std::size_t line_count(const std::string &text) {
  return static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
}

std::vector<std::pair<double, double>> wave(std::size_t n) {
  Sine sine(2.0, 1.0);
  sine.set_noiseless();
  sine.set_sample_rate(static_cast<double>(n));
  return sine.take_series(n);
}

bool has_high_bytes(const std::string &text) {
  return std::any_of(text.begin(), text.end(), [](char c) {
    return static_cast<unsigned char>(c) >= 0x80;
  });
}

} // namespace

TEST_CASE("an empty series renders nothing") { CHECK(plot({}) == ""); }

TEST_CASE("the plot has one line per row, plus the two axis lines") {
  PlotOptions options;
  options.height = 12;
  CHECK(line_count(plot(wave(200), options)) == 12 + 2);

  options.axes = false;
  CHECK(line_count(plot(wave(200), options)) == 12);
}

TEST_CASE("braille cells are used unless ASCII is requested") {
  PlotOptions options;
  CHECK(has_high_bytes(plot(wave(200), options)));

  options.ascii = true;
  const std::string ascii = plot(wave(200), options);
  CHECK_FALSE(has_high_bytes(ascii));
  CHECK(ascii.find('*') != std::string::npos);
}

TEST_CASE("rows are the requested width in characters") {
  PlotOptions options;
  options.width = 40;
  options.height = 5;
  options.ascii = true;
  const std::string text = plot(wave(100), options);
  // Every plot row is label_width characters of gutter plus the plot area.
  std::size_t start = 0;
  for (std::size_t row = 0; row < options.height; ++row) {
    const std::size_t end = text.find('\n', start);
    REQUIRE(end != std::string::npos);
    CHECK(end - start == options.width);
    start = end + 1;
  }
}

TEST_CASE("the vertical range can be fitted or forced") {
  PlotOptions fitted;
  fitted.ascii = true;
  const std::string automatic = plot(wave(200), fitted);
  CHECK(automatic.find("1") != std::string::npos);

  PlotOptions forced = fitted;
  forced.y_min = -10.0;
  forced.y_max = 10.0;
  const std::string manual = plot(wave(200), forced);
  CHECK(manual.find("10") != std::string::npos);
  // Confined to the middle of a much taller window, the trace occupies fewer
  // rows than when the range hugs the data.
  CHECK(std::count(manual.begin(), manual.end(), '*') <
        std::count(automatic.begin(), automatic.end(), '*'));
}

TEST_CASE("a constant signal does not divide by zero") {
  std::vector<std::pair<double, double>> flat;
  for (std::size_t i = 0; i < 50; ++i)
    flat.emplace_back(static_cast<double>(i) * 0.1, 3.0);
  const std::string text = plot(flat);
  CHECK(line_count(text) > 0);
  CHECK(text.find("3.5") != std::string::npos);
  CHECK(text.find("2.5") != std::string::npos);
}

TEST_CASE("a single sample renders without running off the canvas") {
  CHECK(line_count(plot({{0.0, 1.0}})) > 0);
}

TEST_CASE("fewer samples than columns still draws a connected trace") {
  PlotOptions options;
  options.ascii = true;
  options.width = 60;
  options.height = 8;
  // Eight samples across fifty-odd columns: the gaps must be interpolated.
  const std::string text = plot(wave(8), options);
  const auto marks = std::count(text.begin(), text.end(), '*');
  CHECK(marks > 40);
}

TEST_CASE("the time axis is labelled with the span of the series") {
  const std::string text = plot(wave(100));
  CHECK(text.find(" s") != std::string::npos);
  CHECK(text.find("0.99") != std::string::npos);
}

TEST_CASE("the evenly spaced overload timestamps from the sampling period") {
  Sine sine(2.0, 1.0);
  sine.set_noiseless();
  sine.set_sample_rate(100.0);
  const std::string from_values = plot(sine.take(100), 0.01);
  sine.reset();
  const std::string from_series = plot(sine.take_series(100));
  CHECK(from_values == from_series);
}
