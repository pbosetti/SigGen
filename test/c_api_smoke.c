/* SigGen -- smoke test for the plain C ABI, compiled as C.
 *
 * Copyright 2026 Paolo Bosetti
 * SPDX-License-Identifier: Apache-2.0
 *
 * The doctest suite covers the library's behaviour; what this adds is proof
 * that siggen_c.h is valid C, that the extern "C" symbols link, and that the
 * ownership rules in the header hold. It deliberately does not use assert(),
 * which a release build would compile away.
 */

#include <siggen_c.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, what)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));         \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

#define CLOSE(a, b) (fabs((a) - (b)) < 1e-9)

static void test_errors(void) {
  char *error = NULL;
  siggen_signal_t *sig = siggen_create("{ not json", &error);
  CHECK(sig == NULL, "malformed JSON must not produce a signal");
  CHECK(error != NULL && strlen(error) > 0, "a diagnostic must be returned");
  siggen_free_string(error);

  error = NULL;
  sig = siggen_create("{\"type\":\"no_such_signal\"}", &error);
  CHECK(sig == NULL, "an unknown type must not produce a signal");
  CHECK(error != NULL && strstr(error, "no_such_signal") != NULL,
        "the diagnostic must name the offending type");
  siggen_free_string(error);

  /* NULL is tolerated everywhere rather than crashing. */
  CHECK(siggen_create(NULL, NULL) == NULL, "NULL json must be rejected");
  CHECK(isnan(siggen_next(NULL)), "next() on NULL must be NaN");
  CHECK(siggen_take(NULL, NULL, 0) != 0, "take() on NULL must fail");
  CHECK(siggen_last_error(NULL) == NULL, "last_error(NULL) must be NULL");
  siggen_destroy(NULL);
  siggen_free_string(NULL);
}

static void test_stream(void) {
  char *error = NULL;
  siggen_signal_t *sig = siggen_create(
      "{\"type\":\"sine\",\"frequency\":10,\"noiseless\":true}", &error);
  CHECK(sig != NULL, "a valid document must produce a signal");
  CHECK(error == NULL, "out_error must be left NULL on success");
  if (!sig)
    return;

  CHECK(strcmp(siggen_type(sig), "sine") == 0, "type must be reported");
  CHECK(siggen_set_sample_rate(sig, 1000.0) == 0, "sample rate must be set");
  CHECK(CLOSE(siggen_sample_rate(sig), 1000.0), "sample rate must round-trip");
  CHECK(CLOSE(siggen_time_step(sig), 0.001), "time step follows the rate");

  /* A rate of zero is refused, and the reason is retrievable. */
  CHECK(siggen_set_sample_rate(sig, 0.0) != 0, "a zero rate must be refused");
  CHECK(strlen(siggen_last_error(sig)) > 0, "the refusal must be explained");
  CHECK(CLOSE(siggen_sample_rate(sig), 1000.0),
        "a refusal must change nothing");

  double first[8], second[8];
  CHECK(siggen_take(sig, first, 8) == 0, "take must succeed");
  CHECK(strlen(siggen_last_error(sig)) == 0, "success must clear the error");
  CHECK(CLOSE(siggen_time(sig), 0.008),
        "the clock must advance with the samples");

  /* A noiseless sine starts at zero and rises. */
  CHECK(CLOSE(first[0], 0.0), "a sine starts at zero");
  CHECK(first[1] > first[0], "and rises");

  CHECK(siggen_reset(sig) == 0, "reset must succeed");
  CHECK(CLOSE(siggen_time(sig), 0.0), "reset must rewind the clock");
  siggen_take(sig, second, 8);
  for (int i = 0; i < 8; ++i)
    CHECK(first[i] == second[i], "reset must reproduce the stream exactly");

  siggen_destroy(sig);
}

static void test_noise_and_seed(void) {
  char *error = NULL;
  siggen_signal_t *sig =
      siggen_create("{\"type\":\"white_noise\",\"sigma\":1.0}", &error);
  CHECK(sig != NULL, "a noise signal must be built");
  if (!sig)
    return;

  double snr = 0.0;
  CHECK(siggen_snr_db(sig, &snr) == 0, "noise carries no intrinsic noise");

  siggen_set_seed(sig, 4242);
  CHECK(siggen_seed(sig) == 4242u, "the seed must round-trip");

  double a[64], b[64];
  siggen_take(sig, a, 64);
  siggen_set_seed(sig, 4242);
  siggen_take(sig, b, 64);
  for (int i = 0; i < 64; ++i)
    CHECK(a[i] == b[i], "one seed must give one stream");

  siggen_set_seed(sig, 4243);
  siggen_take(sig, b, 64);
  int differences = 0;
  for (int i = 0; i < 64; ++i)
    if (a[i] != b[i])
      ++differences;
  CHECK(differences > 60, "a different seed must give a different stream");

  siggen_destroy(sig);
}

static void test_snr_and_clone(void) {
  char *error = NULL;
  siggen_signal_t *sig =
      siggen_create("{\"type\":\"sine\",\"frequency\":10}", &error);
  if (!sig) {
    CHECK(0, "a default sine must be built");
    return;
  }

  /* The library's promise: a signal is never perfect unless asked. */
  double snr = 0.0;
  CHECK(siggen_snr_db(sig, &snr) == 1, "a sine carries intrinsic noise");
  CHECK(CLOSE(snr, 40.0), "at the documented default of 40 dB");
  CHECK(CLOSE(siggen_rms(sig), 1.0 / sqrt(2.0)), "with the analytic RMS");

  CHECK(siggen_set_snr_db(sig, 12.0) == 0, "the ratio must be settable");
  CHECK(siggen_snr_db(sig, &snr) == 1 && CLOSE(snr, 12.0), "and round-trip");
  CHECK(siggen_set_noiseless(sig) == 0, "noise must be switchable off");
  CHECK(siggen_snr_db(sig, NULL) == 0, "and then report as absent");

  /* A clone continues from where the original had reached, independently. */
  double warmup[4];
  siggen_take(sig, warmup, 4);
  siggen_signal_t *copy = siggen_clone(sig);
  CHECK(copy != NULL, "a signal must be cloneable");
  if (copy) {
    double from_original[16], from_copy[16];
    siggen_take(sig, from_original, 16);
    siggen_take(copy, from_copy, 16);
    for (int i = 0; i < 16; ++i)
      CHECK(from_original[i] == from_copy[i], "a clone continues identically");
    siggen_destroy(copy);
  }
  siggen_destroy(sig);
}

static void test_series_and_plot(void) {
  char *error = NULL;
  /* An expression in a parameter, evaluated by Expressionist on the way in. */
  siggen_signal_t *sig =
      siggen_create("{\"sample_rate\":100,\"f0\":5,"
                    " \"signal\":{\"type\":\"sine\",\"frequency\":\"$f0 * 2\","
                    "             \"noiseless\":true}}",
                    &error);
  CHECK(sig != NULL, "an expression must be evaluated, not rejected");
  if (!sig) {
    fprintf(stderr, "  (%s)\n", error ? error : "no message");
    siggen_free_string(error);
    return;
  }
  CHECK(CLOSE(siggen_sample_rate(sig), 100.0), "sample_rate must be applied");

  double t[100], y[100];
  CHECK(siggen_take_series(sig, t, y, 100) == 0, "take_series must succeed");
  CHECK(CLOSE(t[0], 0.0) && CLOSE(t[1], 0.01), "timestamps follow the rate");

  /* 10 Hz at 100 Hz sampling: one period every ten samples. */
  CHECK(CLOSE(y[0], y[10]), "the waveform must repeat over its period");

  siggen_plot_options_t options;
  siggen_plot_options_init(&options);
  options.width = 60;
  options.height = 8;
  options.ascii = 1;
  char *picture = siggen_plot(t, y, 100, &options);
  CHECK(picture != NULL && strlen(picture) > 0, "a plot must be rendered");
  CHECK(picture && strchr(picture, '*') != NULL, "ASCII mode must use marks");
  siggen_free_string(picture);

  char *braille = siggen_plot_values(y, 100, 0.01, NULL);
  CHECK(braille != NULL && strstr(braille, "\xe2\xa0") != NULL,
        "the default must be braille");
  siggen_free_string(braille);

  siggen_destroy(sig);
}

int main(void) {
  printf("siggen_c version %s\n", siggen_version());
  test_errors();
  test_stream();
  test_noise_and_seed();
  test_snr_and_clone();
  test_series_and_plot();
  if (failures == 0)
    printf("C ABI smoke test: all checks passed\n");
  else
    fprintf(stderr, "C ABI smoke test: %d check(s) failed\n", failures);
  return failures == 0 ? 0 : 1;
}
