// SigGen -- ASCII-art rendering of a sampled signal.
//
// Copyright 2026 Paolo Bosetti
// SPDX-License-Identifier: Apache-2.0
//
// Braille cells pack 2x4 dots into one character, so a plot gets eight times
// the resolution of a naive character grid while staying plain text -- the
// trick btop uses. Terminals whose font lacks the block fall back to a coarse
// pure-ASCII grid.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace SigGen {

/// Knobs for plot(). The defaults suit a standard 80-column terminal.
struct PlotOptions {
  /// Total width in characters, axis labels included.
  std::size_t width = 80;
  /// Height of the plot area in character rows.
  std::size_t height = 15;
  /// Render with '*' on a coarse grid instead of braille dots.
  bool ascii = false;
  /// Draw the axes and their labels.
  bool axes = true;
  /// Force the vertical range instead of fitting it to the data.
  std::optional<double> y_min;
  std::optional<double> y_max;
  /// Columns reserved for the y-axis labels, the axis rule included.
  std::size_t label_width = 9;
  /// Unit shown under the horizontal axis.
  std::string x_unit = "s";
};

namespace detail {

/// Dot bit for each (row, column) position inside a braille cell. The block's
/// first six dots are laid out column-major, but dots 7 and 8 -- added later
/// for eight-dot braille -- were appended at the top of the byte, so the
/// bottom row does not follow the pattern of the three above it.
inline constexpr std::array<std::array<std::uint8_t, 2>, 4> braille_dots = {
    {{0x01, 0x08}, {0x02, 0x10}, {0x04, 0x20}, {0x40, 0x80}}};

/// Append U+2800 + mask, encoded as UTF-8.
inline void append_braille(std::string &out, std::uint8_t mask) {
  const unsigned code = 0x2800u + mask;
  out.push_back(static_cast<char>(0xE0u | (code >> 12)));
  out.push_back(static_cast<char>(0x80u | ((code >> 6) & 0x3Fu)));
  out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
}

/// Format a value to fit `width` characters, shedding significant digits
/// rather than being cut off: a truncated number reads as a wrong one.
inline std::string format_number(double value, std::size_t width = 32) {
  char buffer[32];
  for (int precision = 4; precision >= 1; --precision) {
    std::snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
    if (std::string(buffer).size() <= width)
      break;
  }
  return buffer;
}

inline std::string pad_left(std::string text, std::size_t width) {
  if (text.size() >= width)
    return text.substr(0, width);
  return std::string(width - text.size(), ' ') + text;
}

/// Vertical extent of the trace in one dot column, as inclusive dot rows.
struct Extent {
  int lo = 0;
  int hi = 0;
  bool used = false;
};

} // namespace detail

/// Render a (time, value) series as a multi-line string, newline-terminated.
inline std::string plot(const std::vector<std::pair<double, double>> &series,
                        const PlotOptions &options = {}) {
  if (series.empty())
    return "";

  const std::size_t label_width = options.axes ? options.label_width : 0;
  const std::size_t columns =
      options.width > label_width + 4 ? options.width - label_width : 4;
  const std::size_t rows = std::max<std::size_t>(options.height, 1);

  // Braille packs 2x4 dots per cell; the ASCII fallback has one per cell.
  const std::size_t dots_x = options.ascii ? columns : 2 * columns;
  const std::size_t dots_y = options.ascii ? rows : 4 * rows;

  double y_lo = options.y_min.value_or(std::numeric_limits<double>::max());
  double y_hi = options.y_max.value_or(std::numeric_limits<double>::lowest());
  if (!options.y_min.has_value() || !options.y_max.has_value()) {
    for (const auto &[t, y] : series) {
      if (!options.y_min.has_value())
        y_lo = std::min(y_lo, y);
      if (!options.y_max.has_value())
        y_hi = std::max(y_hi, y);
    }
  }
  if (!(y_hi > y_lo)) {
    // A constant signal has no extent to scale to; give it a unit window so
    // the trace lands on a sensible row instead of dividing by zero.
    const double centre = std::isfinite(y_lo) ? y_lo : 0.0;
    y_lo = centre - 0.5;
    y_hi = centre + 0.5;
  }

  const auto row_of = [&](double y) {
    const double fraction = (y_hi - y) / (y_hi - y_lo);
    const double row = fraction * static_cast<double>(dots_y - 1);
    return std::clamp(static_cast<int>(std::lround(row)), 0,
                      static_cast<int>(dots_y) - 1);
  };

  // Bucket the samples by column, keeping the extremes: a column spanning many
  // samples then shows the full excursion rather than one arbitrary value.
  std::vector<detail::Extent> extents(dots_x);
  const std::size_t n = series.size();
  for (std::size_t i = 0; i < n; ++i) {
    std::size_t column = n == 1 ? 0 : i * dots_x / n;
    column = std::min(column, dots_x - 1);
    const int row = row_of(series[i].second);
    detail::Extent &e = extents[column];
    if (!e.used) {
      e = {row, row, true};
    } else {
      e.lo = std::min(e.lo, row);
      e.hi = std::max(e.hi, row);
    }
  }

  // With fewer samples than columns some buckets stay empty; interpolate the
  // series there so the trace does not come out dotted.
  for (std::size_t column = 0; column < dots_x; ++column) {
    if (extents[column].used)
      continue;
    const double position = dots_x == 1 ? 0.0
                                        : static_cast<double>(column) *
                                              static_cast<double>(n - 1) /
                                              static_cast<double>(dots_x - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(position));
    const std::size_t hi = std::min(lo + 1, n - 1);
    const double fraction = position - static_cast<double>(lo);
    const double value =
        series[lo].second + fraction * (series[hi].second - series[lo].second);
    const int row = row_of(value);
    extents[column] = {row, row, true};
  }

  // Join neighbouring columns so a steep edge draws as a line rather than two
  // disconnected marks.
  for (std::size_t column = 1; column < dots_x; ++column) {
    detail::Extent &previous = extents[column - 1];
    detail::Extent &current = extents[column];
    if (current.lo > previous.hi)
      current.lo = previous.hi;
    else if (current.hi < previous.lo)
      current.hi = previous.lo;
  }

  std::vector<std::uint8_t> canvas(columns * rows, 0);
  std::vector<char> ascii_canvas(columns * rows, ' ');
  for (std::size_t column = 0; column < dots_x; ++column) {
    for (int row = extents[column].lo; row <= extents[column].hi; ++row) {
      if (options.ascii) {
        ascii_canvas[static_cast<std::size_t>(row) * columns + column] = '*';
      } else {
        const std::size_t cell =
            (static_cast<std::size_t>(row) / 4) * columns + column / 2;
        canvas[cell] |=
            detail::braille_dots[static_cast<std::size_t>(row) % 4][column % 2];
      }
    }
  }

  // The third label goes on the zero line whenever zero is on the canvas --
  // far more informative than the arithmetic midpoint of the window, which is
  // rarely a round number once a noise floor has widened the range.
  const bool middle_is_zero = y_lo < 0.0 && y_hi > 0.0;
  const std::size_t dots_per_row = options.ascii ? 1 : 4;
  const std::size_t candidate_row =
      middle_is_zero ? static_cast<std::size_t>(row_of(0.0)) / dots_per_row
                     : rows / 2;
  const std::size_t middle_row =
      (rows > 2 && candidate_row > 0 && candidate_row + 1 < rows)
          ? candidate_row
          : rows;

  std::string out;
  out.reserve(rows * (columns * 3 + label_width + 2));
  for (std::size_t row = 0; row < rows; ++row) {
    if (options.axes) {
      std::string label;
      if (row == 0)
        label = detail::format_number(y_hi, label_width - 2);
      else if (row + 1 == rows)
        label = detail::format_number(y_lo, label_width - 2);
      else if (row == middle_row)
        label = middle_is_zero ? "0"
                               : detail::format_number(0.5 * (y_hi + y_lo),
                                                       label_width - 2);
      out += detail::pad_left(label, label_width - 2);
      out += " |";
    }
    for (std::size_t column = 0; column < columns; ++column) {
      if (options.ascii)
        out.push_back(ascii_canvas[row * columns + column]);
      else
        detail::append_braille(out, canvas[row * columns + column]);
    }
    out.push_back('\n');
  }

  if (options.axes) {
    out += std::string(label_width - 1, ' ');
    out.push_back('+');
    out += std::string(columns, '-');
    out.push_back('\n');

    const std::string left = detail::format_number(series.front().first);
    const std::string right =
        detail::format_number(series.back().first) + " " + options.x_unit;
    out += std::string(label_width, ' ');
    out += left;
    if (columns > left.size() + right.size())
      out += std::string(columns - left.size() - right.size(), ' ');
    else
      out.push_back(' ');
    out += right;
    out.push_back('\n');
  }
  return out;
}

/// Render evenly spaced values, timestamping them from the sampling period.
inline std::string plot(const std::vector<double> &values, double time_step,
                        const PlotOptions &options = {}) {
  std::vector<std::pair<double, double>> series;
  series.reserve(values.size());
  for (std::size_t i = 0; i < values.size(); ++i)
    series.emplace_back(static_cast<double>(i) * time_step, values[i]);
  return plot(series, options);
}

} // namespace SigGen
