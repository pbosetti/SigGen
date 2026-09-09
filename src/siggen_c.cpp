// SigGen -- implementation of the plain C ABI declared in siggen_c.h.
//
// Copyright 2026 Paolo Bosetti
// SPDX-License-Identifier: Apache-2.0
//
// Every entry point is a thin translation layer: it validates its arguments,
// calls into the header-only C++ library and converts whatever comes back --
// including any exception -- into a plain C result. Nothing here carries
// signal-processing logic of its own.

#include "siggen_c.h"

#include <siggen.hpp>

#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifndef SIGGEN_VERSION
#define SIGGEN_VERSION "unknown"
#endif

/// The opaque handle: the generator, plus the two strings the ABI hands out by
/// pointer and therefore has to keep alive somewhere.
struct siggen_signal {
  std::unique_ptr<SigGen::Signal> signal;
  std::string type;
  std::string last_error;
};

namespace {

constexpr double not_a_number = std::numeric_limits<double>::quiet_NaN();

/// Copy a std::string onto the C heap, for release by siggen_free_string().
/// malloc rather than new[], so a caller in another language can free it
/// through its own bindings to the C allocator if it would rather.
char *duplicate(const std::string &text) {
  char *out = static_cast<char *>(std::malloc(text.size() + 1));
  if (out)
    std::memcpy(out, text.c_str(), text.size() + 1);
  return out;
}

void report(char **out_error, const std::string &message) {
  if (out_error)
    *out_error = duplicate(message);
}

/// Record a failure against a handle and return the ABI's failure code.
int fail(siggen_signal_t *sig, const char *message) {
  if (sig)
    sig->last_error = message;
  return 1;
}

int succeed(siggen_signal_t *sig) {
  sig->last_error.clear();
  return 0;
}

/// Wrap a freshly built C++ signal in a handle.
siggen_signal_t *wrap(std::unique_ptr<SigGen::Signal> signal,
                      char **out_error) {
  auto *handle = new (std::nothrow) siggen_signal;
  if (!handle) {
    report(out_error, "out of memory");
    return nullptr;
  }
  handle->type = signal->type();
  handle->signal = std::move(signal);
  return handle;
}

SigGen::PlotOptions convert(const siggen_plot_options_t *options) {
  SigGen::PlotOptions out;
  if (!options)
    return out;
  out.width = options->width;
  out.height = options->height;
  out.ascii = options->ascii != 0;
  out.axes = options->axes != 0;
  if (options->has_y_min)
    out.y_min = options->y_min;
  if (options->has_y_max)
    out.y_max = options->y_max;
  out.label_width = options->label_width;
  if (options->x_unit)
    out.x_unit = options->x_unit;
  return out;
}

} // namespace

extern "C" {

const char *siggen_last_error(const siggen_signal_t *sig) {
  return sig ? sig->last_error.c_str() : nullptr;
}

void siggen_free_string(char *s) { std::free(s); }

siggen_signal_t *siggen_create(const char *json, char **out_error) {
  if (out_error)
    *out_error = nullptr;
  if (!json) {
    report(out_error, "siggen_create: json must not be NULL");
    return nullptr;
  }
  try {
    return wrap(SigGen::from_string(json), out_error);
  } catch (const std::exception &e) {
    report(out_error, e.what());
  } catch (...) {
    report(out_error, "unknown error");
  }
  return nullptr;
}

siggen_signal_t *siggen_create_from_file(const char *path, char **out_error) {
  if (out_error)
    *out_error = nullptr;
  if (!path) {
    report(out_error, "siggen_create_from_file: path must not be NULL");
    return nullptr;
  }
  try {
    return wrap(SigGen::from_file(path), out_error);
  } catch (const std::exception &e) {
    report(out_error, e.what());
  } catch (...) {
    report(out_error, "unknown error");
  }
  return nullptr;
}

siggen_signal_t *siggen_clone(const siggen_signal_t *sig) {
  if (!sig || !sig->signal)
    return nullptr;
  try {
    return wrap(sig->signal->clone(), nullptr);
  } catch (...) {
    return nullptr;
  }
}

void siggen_destroy(siggen_signal_t *sig) { delete sig; }

int siggen_take(siggen_signal_t *sig, double *out, size_t n) {
  if (!sig)
    return 1;
  if (!out)
    return fail(sig, "siggen_take: out must not be NULL");
  try {
    for (size_t i = 0; i < n; ++i)
      out[i] = sig->signal->next();
    return succeed(sig);
  } catch (const std::exception &e) {
    return fail(sig, e.what());
  } catch (...) {
    return fail(sig, "unknown error");
  }
}

int siggen_take_series(siggen_signal_t *sig, double *t, double *y, size_t n) {
  if (!sig)
    return 1;
  try {
    for (size_t i = 0; i < n; ++i) {
      const double now = sig->signal->time();
      const double value = sig->signal->next();
      if (t)
        t[i] = now;
      if (y)
        y[i] = value;
    }
    return succeed(sig);
  } catch (const std::exception &e) {
    return fail(sig, e.what());
  } catch (...) {
    return fail(sig, "unknown error");
  }
}

double siggen_next(siggen_signal_t *sig) {
  if (!sig)
    return not_a_number;
  try {
    return sig->signal->next();
  } catch (...) {
    return not_a_number;
  }
}

int siggen_reset(siggen_signal_t *sig) {
  if (!sig)
    return 1;
  try {
    sig->signal->reset();
    return succeed(sig);
  } catch (const std::exception &e) {
    return fail(sig, e.what());
  } catch (...) {
    return fail(sig, "unknown error");
  }
}

int siggen_set_sample_rate(siggen_signal_t *sig, double fs) {
  if (!sig)
    return 1;
  try {
    sig->signal->set_sample_rate(fs);
    return succeed(sig);
  } catch (const std::exception &e) {
    return fail(sig, e.what());
  } catch (...) {
    return fail(sig, "unknown error");
  }
}

double siggen_sample_rate(const siggen_signal_t *sig) {
  return sig ? sig->signal->sample_rate() : not_a_number;
}

int siggen_set_seed(siggen_signal_t *sig, uint64_t seed) {
  if (!sig)
    return 1;
  try {
    sig->signal->set_seed(seed);
    return succeed(sig);
  } catch (const std::exception &e) {
    return fail(sig, e.what());
  } catch (...) {
    return fail(sig, "unknown error");
  }
}

uint64_t siggen_seed(const siggen_signal_t *sig) {
  return sig ? sig->signal->seed() : 0;
}

int siggen_set_snr_db(siggen_signal_t *sig, double snr_db) {
  if (!sig)
    return 1;
  sig->signal->set_snr_db(snr_db);
  return succeed(sig);
}

int siggen_set_noiseless(siggen_signal_t *sig) {
  if (!sig)
    return 1;
  sig->signal->set_noiseless();
  return succeed(sig);
}

int siggen_snr_db(const siggen_signal_t *sig, double *out_snr_db) {
  if (!sig)
    return 0;
  const std::optional<double> snr = sig->signal->snr_db();
  if (!snr.has_value())
    return 0;
  if (out_snr_db)
    *out_snr_db = *snr;
  return 1;
}

const char *siggen_type(const siggen_signal_t *sig) {
  return sig ? sig->type.c_str() : nullptr;
}

double siggen_rms(const siggen_signal_t *sig) {
  return sig ? sig->signal->rms() : not_a_number;
}

double siggen_time(const siggen_signal_t *sig) {
  return sig ? sig->signal->time() : not_a_number;
}

double siggen_time_step(const siggen_signal_t *sig) {
  return sig ? sig->signal->time_step() : not_a_number;
}

void siggen_plot_options_init(siggen_plot_options_t *options) {
  if (!options)
    return;
  const SigGen::PlotOptions defaults;
  options->width = defaults.width;
  options->height = defaults.height;
  options->ascii = defaults.ascii ? 1 : 0;
  options->axes = defaults.axes ? 1 : 0;
  options->has_y_min = 0;
  options->y_min = 0.0;
  options->has_y_max = 0;
  options->y_max = 0.0;
  options->label_width = defaults.label_width;
  options->x_unit = nullptr;
}

char *siggen_plot(const double *t, const double *y, size_t n,
                  const siggen_plot_options_t *options) {
  if (!t || !y)
    return nullptr;
  try {
    std::vector<std::pair<double, double>> series;
    series.reserve(n);
    for (size_t i = 0; i < n; ++i)
      series.emplace_back(t[i], y[i]);
    return duplicate(SigGen::plot(series, convert(options)));
  } catch (...) {
    return nullptr;
  }
}

char *siggen_plot_values(const double *y, size_t n, double time_step,
                         const siggen_plot_options_t *options) {
  if (!y)
    return nullptr;
  try {
    const std::vector<double> values(y, y + n);
    return duplicate(SigGen::plot(values, time_step, convert(options)));
  } catch (...) {
    return nullptr;
  }
}

const char *siggen_version(void) { return SIGGEN_VERSION; }

} // extern "C"
