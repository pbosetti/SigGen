/*! \file siggen_c.h
 *  \brief Plain C ABI for SigGen: build a signal from JSON, then stream it.
 *
 * Copyright 2026 Paolo Bosetti
 * SPDX-License-Identifier: Apache-2.0
 *
 * A language-agnostic wrapper around the header-only C++ library, consumable
 * from any runtime with a C FFI (ctypes/cffi in Python, but also Ruby, Node,
 * Rust...). No C++ type -- std::string, std::unique_ptr, nlohmann::json, an
 * exception -- ever crosses this boundary.
 *
 * A signal is described entirely by a JSON document, the same one the C++
 * SigGen::from_json() accepts and the same one the `siggen` tool reads, so
 * every waveform, noise colour, ARIMA process, table and composite is
 * reachable through the single constructor below. Algebraic expressions in the
 * document are evaluated as usual.
 *
 * Unlike Expressionist's stateless JSON-in/JSON-out ABI, a signal here is a
 * stateful stream: the handle owns the phase, the filter state and the random
 * sequence, and every call to siggen_take() continues where the last one
 * stopped. A handle must therefore be used by one thread at a time.
 *
 * \section perf Prefer the bulk calls
 *
 * siggen_next() exists for convenience, but one FFI call per sample costs far
 * more than generating the sample does -- through ctypes, on the order of a
 * hundred times more. Fill a buffer with siggen_take() instead and let the
 * loop run on this side of the boundary.
 *
 * \code{.c}
 * #include <siggen_c.h>
 * #include <stdio.h>
 * #include <stdlib.h>
 *
 * int main(void) {
 *   char *error = NULL;
 *   siggen_signal_t *sig = siggen_create(
 *       "{\"type\":\"sine\",\"frequency\":50,\"snr_db\":30}", &error);
 *   if (!sig) {
 *     fprintf(stderr, "error: %s\n", error);
 *     siggen_free_string(error);
 *     return 1;
 *   }
 *
 *   siggen_set_sample_rate(sig, 1000.0);
 *   siggen_set_seed(sig, 42);
 *
 *   double buffer[500];
 *   siggen_take(sig, buffer, 500);
 *   printf("%.6f\n", buffer[1]);
 *
 *   siggen_destroy(sig);
 *   return 0;
 * }
 * \endcode
 */

#ifndef SIGGEN_C_H
#define SIGGEN_C_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef SIGGEN_C_BUILDING
#define SIGGEN_C_API __declspec(dllexport)
#else
#define SIGGEN_C_API __declspec(dllimport)
#endif
#else
#define SIGGEN_C_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*! An opaque signal: the generator plus the stream state it has reached.
 *  Create with siggen_create(), release with siggen_destroy(). */
typedef struct siggen_signal siggen_signal_t;

/*! \name Error reporting
 *  Two conventions, each used where it fits. A function that *creates*
 *  something reports through a `char **out_error` out-parameter, heap
 *  allocated, which the caller releases with siggen_free_string(). A function
 *  operating on an existing handle returns nonzero and leaves the detail in
 *  siggen_last_error(), owned by the handle and valid until the next call on
 *  it -- so an argument check costs no allocation.
 *  @{ */

/*! \return the message describing the most recent failed call on `sig`, or an
 *  empty string if the last call succeeded. Owned by the handle: do not free
 *  it, and do not keep it past the next call on the same handle. NULL only if
 *  `sig` is NULL. */
SIGGEN_C_API const char *siggen_last_error(const siggen_signal_t *sig);

/*! Frees a string returned through an `out_error` out-parameter, or by
 *  siggen_plot(). Accepts NULL. */
SIGGEN_C_API void siggen_free_string(char *s);
/*! @} */

/*! \name Lifecycle
 *  @{ */

/*! Builds a signal from a JSON document -- either a bare signal object with a
 *  "type" key, or a wrapper carrying "signal" plus "sample_rate" and "seed".
 *
 *  \param json UTF-8 JSON text
 *  \param out_error set to a diagnostic message on failure; may be NULL if the
 *         detail is not wanted
 *  \return a new signal, or NULL on failure (malformed JSON, unknown type,
 *          out-of-range parameter, bad expression) */
SIGGEN_C_API siggen_signal_t *siggen_create(const char *json, char **out_error);

/*! As siggen_create(), reading the document from a file. */
SIGGEN_C_API siggen_signal_t *siggen_create_from_file(const char *path,
                                                      char **out_error);

/*! An independent copy of `sig`, including the stream position it has reached.
 *  \return a new signal, or NULL if `sig` is NULL or allocation fails. */
SIGGEN_C_API siggen_signal_t *siggen_clone(const siggen_signal_t *sig);

/*! Releases a signal. Accepts NULL. */
SIGGEN_C_API void siggen_destroy(siggen_signal_t *sig);
/*! @} */

/*! \name Generation
 *  @{ */

/*! Writes the next `n` samples into `out`, advancing the stream by `n`.
 *  The caller owns `out` and it must hold at least `n` doubles.
 *  \return 0 on success, nonzero if `sig` or `out` is NULL */
SIGGEN_C_API int siggen_take(siggen_signal_t *sig, double *out, size_t n);

/*! As siggen_take(), also writing each sample's timestamp in seconds into `t`.
 *  Either array may be NULL to skip it.
 *  \return 0 on success, nonzero if `sig` is NULL */
SIGGEN_C_API int siggen_take_series(siggen_signal_t *sig, double *t, double *y,
                                    size_t n);

/*! One sample, advancing the stream by one. Convenient, but see \ref perf.
 *  \return the sample, or NaN if `sig` is NULL */
SIGGEN_C_API double siggen_next(siggen_signal_t *sig);

/*! Rewinds the stream: the clock returns to zero and the random sequence
 *  restarts from the current seed, so the samples repeat exactly.
 *  \return 0 on success, nonzero if `sig` is NULL */
SIGGEN_C_API int siggen_reset(siggen_signal_t *sig);
/*! @} */

/*! \name Configuration
 *  @{ */

/*! Sets the sampling frequency in hertz, which must be positive.
 *  \return 0 on success, nonzero on failure (see siggen_last_error()) */
SIGGEN_C_API int siggen_set_sample_rate(siggen_signal_t *sig, double fs);

/*! \return the sampling frequency in hertz, or NaN if `sig` is NULL */
SIGGEN_C_API double siggen_sample_rate(const siggen_signal_t *sig);

/*! Reseeds the random stream, which also rewinds the signal.
 *  \return 0 on success, nonzero if `sig` is NULL */
SIGGEN_C_API int siggen_set_seed(siggen_signal_t *sig, uint64_t seed);

/*! \return the current seed, or 0 if `sig` is NULL */
SIGGEN_C_API uint64_t siggen_seed(const siggen_signal_t *sig);

/*! Sets the signal-to-noise ratio in decibels of the intrinsic white noise.
 *  \return 0 on success, nonzero if `sig` is NULL */
SIGGEN_C_API int siggen_set_snr_db(siggen_signal_t *sig, double snr_db);

/*! Switches the intrinsic white noise off.
 *  \return 0 on success, nonzero if `sig` is NULL */
SIGGEN_C_API int siggen_set_noiseless(siggen_signal_t *sig);

/*! Reads the signal-to-noise ratio, if the intrinsic noise is on.
 *  \param out_snr_db set to the ratio in decibels when one is set; untouched
 *         otherwise. May be NULL to test only.
 *  \return 1 when a ratio is set, 0 when the signal is noiseless or NULL */
SIGGEN_C_API int siggen_snr_db(const siggen_signal_t *sig, double *out_snr_db);
/*! @} */

/*! \name Introspection
 *  @{ */

/*! \return the type name ("sine", "arima", "composite"...), owned by the
 *  handle, or NULL if `sig` is NULL */
SIGGEN_C_API const char *siggen_type(const siggen_signal_t *sig);

/*! \return the analytic RMS of the clean waveform, or NaN if `sig` is NULL */
SIGGEN_C_API double siggen_rms(const siggen_signal_t *sig);

/*! \return the timestamp of the next sample in seconds, or NaN if NULL */
SIGGEN_C_API double siggen_time(const siggen_signal_t *sig);

/*! \return the sampling period in seconds, or NaN if `sig` is NULL */
SIGGEN_C_API double siggen_time_step(const siggen_signal_t *sig);
/*! @} */

/*! \name Plotting
 *  @{ */

/*! Options for siggen_plot(). Always initialise with
 *  siggen_plot_options_init() before setting fields, so that members added in
 *  a later version still get sensible values. */
typedef struct {
  size_t width;       /*!< Total width in characters, labels included. */
  size_t height;      /*!< Height of the plot area in character rows. */
  int ascii;          /*!< Nonzero to use '*' marks instead of braille dots. */
  int axes;           /*!< Nonzero to draw the axes and their labels. */
  int has_y_min;      /*!< Nonzero to force the bottom of the range. */
  double y_min;       /*!< Bottom of the range, when has_y_min is nonzero. */
  int has_y_max;      /*!< Nonzero to force the top of the range. */
  double y_max;       /*!< Top of the range, when has_y_max is nonzero. */
  size_t label_width; /*!< Columns reserved for the y-axis labels. */
  const char *x_unit; /*!< Unit under the x axis; NULL means seconds. */
} siggen_plot_options_t;

/*! Fills `options` with the same defaults the C++ SigGen::PlotOptions carries.
 *  A NULL pointer is a silent no-op. */
SIGGEN_C_API void siggen_plot_options_init(siggen_plot_options_t *options);

/*! Renders a (time, value) series as UTF-8 text, newline-terminated.
 *  \param options may be NULL for the defaults
 *  \return a heap-allocated string to release with siggen_free_string(), or
 *          NULL on failure */
SIGGEN_C_API char *siggen_plot(const double *t, const double *y, size_t n,
                               const siggen_plot_options_t *options);

/*! As siggen_plot(), timestamping evenly spaced values from `time_step`. */
SIGGEN_C_API char *siggen_plot_values(const double *y, size_t n,
                                      double time_step,
                                      const siggen_plot_options_t *options);
/*! @} */

/*! \return the library version, statically allocated (do not free it) */
SIGGEN_C_API const char *siggen_version(void);

#ifdef __cplusplus
}
#endif

#endif /* SIGGEN_C_H */
