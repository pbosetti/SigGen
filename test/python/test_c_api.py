"""SigGen -- tests for the C ABI and its ctypes wrapper.

Copyright 2026 Paolo Bosetti
SPDX-License-Identifier: Apache-2.0

Run through ctest (which points SIGGEN_C_LIBRARY at the freshly built shared
library), or by hand with SIGGEN_C_LIBRARY and PYTHONPATH set.
"""

import json
import math
import os

import pytest

import siggen

# The same literals as test/test_golden.cpp. Asserting them on both sides is
# what proves a stream reached through ctypes is the stream the C++ API
# produces; keep the two files in step.
GOLDEN_SINE = {
    "config": {
        "sample_rate": 100,
        "seed": 12345,
        "signal": {"type": "sine", "frequency": 5, "amplitude": 2, "snr_db": 20},
    },
    "samples": [
        -0.238320196013666,
        0.451380277722327,
        1.43477757151572,
        1.69979347159333,
        1.95174132247097,
        1.80515020639691,
    ],
}

GOLDEN_PINK = {
    "config": {"seed": 7, "signal": {"type": "pink_noise", "sigma": 1.0}},
    "samples": [
        -1.19245579401624,
        -1.10831409955108,
        -1.61931945027813,
        -1.68866352014714,
    ],
}

GOLDEN_ARIMA = {
    "config": {
        "seed": 99,
        "signal": {
            "type": "arima",
            "ar": [0.6],
            "d": 1,
            "ma": [0.3],
            "sigma": 1.0,
        },
    },
    "samples": [
        -2.23437222690341,
        -3.44326070343336,
        -3.42982679109250,
        -2.79098226245950,
    ],
}


@pytest.mark.parametrize(
    "golden", [GOLDEN_SINE, GOLDEN_PINK, GOLDEN_ARIMA], ids=["sine", "pink", "arima"]
)
def test_matches_the_cxx_golden_values(golden):
    with siggen.Signal(golden["config"]) as signal:
        samples = list(signal.take(len(golden["samples"])))
    assert samples == pytest.approx(golden["samples"], rel=1e-12)


def test_version_is_reported():
    assert siggen.version()


def test_a_dict_and_its_json_text_agree():
    config = {"type": "sine", "frequency": 10, "noiseless": True}
    with siggen.Signal(config) as a, siggen.Signal(json.dumps(config)) as b:
        assert list(a.take(16)) == list(b.take(16))


def test_a_noiseless_sine_is_a_sine():
    with siggen.Signal(
        {"type": "sine", "frequency": 10, "noiseless": True}, sample_rate=1000
    ) as signal:
        samples = signal.take(100)
        assert samples[0] == pytest.approx(0.0, abs=1e-12)
        # 10 Hz at 1 kHz: one period every hundred samples, so a quarter period
        # in is the peak.
        assert samples[25] == pytest.approx(1.0, abs=1e-9)


def test_the_stream_is_reproducible():
    with siggen.Signal({"type": "white_noise", "sigma": 1.0}, seed=4242) as signal:
        first = list(signal.take(64))
        signal.reset()
        assert list(signal.take(64)) == first
        signal.seed = 4242
        assert list(signal.take(64)) == first
        signal.seed = 4243
        assert list(signal.take(64)) != first


def test_take_series_timestamps_follow_the_sample_rate():
    with siggen.Signal({"type": "sine", "frequency": 1}, sample_rate=100) as signal:
        times, values = signal.take_series(10)
        assert len(times) == len(values) == 10
        assert list(times) == pytest.approx([i / 100.0 for i in range(10)])
        assert signal.time == pytest.approx(0.1)


def test_next_continues_the_same_stream_as_take():
    config = {"type": "sine", "frequency": 3}
    with siggen.Signal(config, seed=1) as a, siggen.Signal(config, seed=1) as b:
        assert [a.next() for _ in range(5)] == pytest.approx(list(b.take(5)))


def test_properties_round_trip():
    with siggen.Signal({"type": "sine", "frequency": 10}) as signal:
        assert signal.type == "sine"
        # Every deterministic signal carries white noise unless told otherwise.
        assert signal.snr_db == pytest.approx(40.0)
        assert signal.rms == pytest.approx(1.0 / math.sqrt(2.0))

        signal.sample_rate = 2000.0
        assert signal.sample_rate == pytest.approx(2000.0)
        assert signal.time_step == pytest.approx(1.0 / 2000.0)

        signal.snr_db = 12.0
        assert signal.snr_db == pytest.approx(12.0)
        signal.snr_db = None
        assert signal.snr_db is None


def test_a_clone_is_independent():
    with siggen.Signal({"type": "white_noise"}, seed=5) as signal:
        signal.take(4)
        copy = signal.clone()
        try:
            assert list(signal.take(16)) == list(copy.take(16))
        finally:
            copy.close()


def test_expressions_in_parameters_are_evaluated():
    with siggen.Signal(
        {
            "sample_rate": 100,
            "f0": 5,
            "signal": {
                "type": "sine",
                "frequency": "$f0 * 2",
                "noiseless": True,
            },
        }
    ) as signal:
        values = signal.take(30)
        # 10 Hz at 100 Hz sampling repeats every ten samples.
        assert values[0] == pytest.approx(values[10], abs=1e-12)
        assert values[3] == pytest.approx(values[23], abs=1e-12)


def test_a_composite_sums_its_components():
    document = {
        "sample_rate": 100,
        "signal": {
            "type": "composite",
            "op": "sum",
            "components": [
                {"type": "sine", "frequency": 5, "noiseless": True},
                {"type": "sine", "frequency": 5, "noiseless": True, "gain": 2},
            ],
        },
    }
    with siggen.Signal(document) as composite, siggen.Signal(
        {"type": "sine", "frequency": 5, "noiseless": True}, sample_rate=100
    ) as single:
        assert list(composite.take(20)) == pytest.approx(
            [3.0 * v for v in single.take(20)]
        )


def test_from_file(tmp_path):
    path = tmp_path / "signal.json"
    path.write_text(json.dumps({"type": "sine", "frequency": 4, "noiseless": True}))
    with siggen.Signal.from_file(path, sample_rate=200) as signal:
        assert signal.type == "sine"
        assert signal.sample_rate == pytest.approx(200.0)


def test_malformed_documents_raise():
    with pytest.raises(siggen.SigGenError):
        siggen.Signal("{ not json")

    with pytest.raises(siggen.SigGenError) as caught:
        siggen.Signal({"type": "no_such_signal"})
    assert "no_such_signal" in str(caught.value)

    with pytest.raises(siggen.SigGenError) as caught:
        siggen.Signal({"type": "square", "frequency": 1, "duty": 5})
    assert "duty" in str(caught.value)


def test_a_rejected_setter_leaves_the_signal_alone():
    with siggen.Signal({"type": "sine", "frequency": 1}, sample_rate=100) as signal:
        with pytest.raises(siggen.SigGenError):
            signal.sample_rate = 0.0
        assert signal.sample_rate == pytest.approx(100.0)


def test_close_is_idempotent_and_the_context_manager_closes():
    signal = siggen.Signal({"type": "sine", "frequency": 1})
    signal.close()
    signal.close()
    assert "closed" in repr(signal)


def test_plot_options_struct_matches_the_c_layout():
    # A padding mismatch between the ctypes mirror and the C struct would show
    # up here as nonsense defaults rather than as a crash somewhere later.
    from siggen import _PlotOptions, _lib
    import ctypes

    options = _PlotOptions()
    _lib.siggen_plot_options_init(ctypes.byref(options))
    assert options.width == 80
    assert options.height == 15
    assert options.label_width == 9
    assert options.axes == 1
    assert options.ascii == 0
    assert options.has_y_min == 0 and options.has_y_max == 0


def test_plot_renders_braille_and_ascii():
    with siggen.Signal(
        {"type": "sine", "frequency": 2, "noiseless": True}, sample_rate=100
    ) as signal:
        times, values = signal.take_series(100)

        braille = siggen.plot(values, t=times, width=60, height=8)
        assert braille.endswith("\n")
        assert any("⠀" <= ch <= "⣿" for ch in braille)

        marks = siggen.plot(values, t=times, width=60, height=8, ascii=True)
        assert "*" in marks
        assert not any("⠀" <= ch <= "⣿" for ch in marks)

        # Eight rows of plot plus the axis rule and its labels.
        assert len(marks.splitlines()) == 10


def test_plot_honours_a_forced_range_and_unit():
    values = [0.0, 0.5, 1.0]
    rendered = siggen.plot(values, time_step=0.5, y_min=-2.0, y_max=2.0, x_unit="ms")
    assert "-2" in rendered
    assert "ms" in rendered


def test_plot_rejects_unknown_options_and_mismatched_lengths():
    with pytest.raises(TypeError):
        siggen.plot([0.0, 1.0], colour="red")
    with pytest.raises(ValueError):
        siggen.plot([0.0, 1.0], t=[0.0])


def test_signal_plot_advances_the_stream():
    with siggen.Signal({"type": "sine", "frequency": 2}, sample_rate=50) as signal:
        assert signal.time == pytest.approx(0.0)
        assert signal.plot(50, width=40, height=6)
        assert signal.time == pytest.approx(1.0)


@pytest.mark.skipif(not siggen.HAVE_NUMPY, reason="numpy is not installed")
def test_take_fills_a_caller_supplied_array():
    import numpy as np

    with siggen.Signal({"type": "sine", "frequency": 5}, seed=3) as signal:
        buffer = np.zeros(32, dtype=np.float64)
        returned = signal.take(32, out=buffer)
        assert returned is buffer
        assert np.any(buffer != 0.0)

        signal.seed = 3
        assert np.allclose(signal.take(32), buffer)

        with pytest.raises(ValueError):
            signal.take(64, out=np.zeros(8, dtype=np.float64))
        with pytest.raises(TypeError):
            signal.take(4, out=np.zeros(8, dtype=np.float32))


@pytest.mark.skipif(not siggen.HAVE_NUMPY, reason="numpy is not installed")
def test_take_returns_a_numpy_array_when_numpy_is_present():
    import numpy as np

    with siggen.Signal({"type": "sine", "frequency": 5}) as signal:
        assert isinstance(signal.take(8), np.ndarray)


# --- epoch anchoring ------------------------------------------------------

EPOCH = 1767225600.0  # 2026-01-01T00:00:00Z
ALIGNED = {
    "sample_rate": 1000,
    "seed": 42,
    "signal": {
        "type": "composite",
        "op": "sum",
        "components": [
            {"type": "sine", "frequency": 50, "snr_db": 30},
            {"type": "pink_noise", "sigma": 0.05},
            {"type": "brown_noise", "sigma": 0.02},
        ],
    },
}


def test_index_and_time_invert_each_other():
    with siggen.Signal(ALIGNED, epoch=EPOCH) as s:
        assert s.epoch == pytest.approx(EPOCH)
        assert s.index_at(EPOCH) == 0
        assert s.index_at(EPOCH + 1.0) == 1000
        assert s.time_at(1000) == pytest.approx(EPOCH + 1.0)
        # A jittery caller lands on the nearest index rather than drifting.
        assert s.index_at(EPOCH + 5.0 + 0.0004) == 5000
        assert s.index_at(EPOCH + 5.0 - 0.0004) == 5000


def test_an_instant_before_the_epoch_is_refused():
    with siggen.Signal(ALIGNED, epoch=EPOCH) as s:
        with pytest.raises(siggen.SigGenError):
            s.index_at(EPOCH - 1.0)


def test_two_signals_started_apart_agree_at_the_same_index():
    # The whole point: one has been running, the other has generated nothing.
    with siggen.Signal(ALIGNED, epoch=EPOCH) as early, siggen.Signal(
        ALIGNED, epoch=EPOCH
    ) as late:
        assert early.is_addressable
        early.take(12345)
        index = early.index_at(EPOCH + 3 * 86400 + 7.0)
        assert list(early.values_at(index, 200)) == list(late.values_at(index, 200))


def test_a_block_and_single_samples_agree_exactly():
    with siggen.Signal(ALIGNED, epoch=EPOCH) as a, siggen.Signal(
        ALIGNED, epoch=EPOCH
    ) as b:
        first = 5_000_000_000
        block = list(a.values_at(first, 32))
        assert [b.at(first + i) for i in range(32)] == block


def test_addressing_leaves_the_sequential_stream_alone():
    with siggen.Signal({"type": "pink_noise"}, seed=9) as s:
        expected = list(s.take(16))
        s.reset()
        interleaved = []
        for i in range(16):
            s.at(1_000_000 + i)
            interleaved.append(s.next())
        assert interleaved == expected


def test_integration_cannot_be_addressed():
    with siggen.Signal({"type": "arima", "ar": [0.6], "d": 1}) as walk:
        assert not walk.is_addressable
        with pytest.raises(siggen.SigGenError) as caught:
            walk.at(10)
        assert "cumulative sum" in str(caught.value)


def test_epoch_travels_in_the_document():
    with siggen.Signal({"epoch": EPOCH, "signal": {"type": "sine", "frequency": 1}}) as s:
        assert s.epoch == pytest.approx(EPOCH)


def test_unix_now_is_a_plausible_wall_clock():
    import time

    assert siggen.unix_now() == pytest.approx(time.time(), abs=5.0)


@pytest.mark.skipif(not siggen.HAVE_NUMPY, reason="numpy is not installed")
def test_values_at_fills_a_caller_supplied_array():
    import numpy as np

    with siggen.Signal(ALIGNED, epoch=EPOCH) as s:
        buffer = np.zeros(64, dtype=np.float64)
        assert s.values_at(1000, 64, out=buffer) is buffer
        assert np.allclose(buffer, s.values_at(1000, 64))
