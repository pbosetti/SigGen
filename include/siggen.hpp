// SigGen -- a header-only generator of synthetic signals for testing.
//
// Copyright 2026 Paolo Bosetti
// SPDX-License-Identifier: Apache-2.0
//
// Umbrella header: including it brings in every generator, the JSON
// configuration front-end and the terminal plotter. Include the individual
// headers under <siggen/> instead to keep a translation unit free of the
// nlohmann/json and Expressionist dependencies, which only <siggen/config.hpp>
// needs.

#pragma once

#include "siggen/arima.hpp"
#include "siggen/composite.hpp"
#include "siggen/config.hpp"
#include "siggen/custom.hpp"
#include "siggen/noise.hpp"
#include "siggen/periodic.hpp"
#include "siggen/plot.hpp"
#include "siggen/rng.hpp"
#include "siggen/signal.hpp"
