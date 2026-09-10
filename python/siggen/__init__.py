# SigGen -- ctypes wrapper around the siggen_c shared library.
#
# Copyright 2026 Paolo Bosetti
# SPDX-License-Identifier: Apache-2.0
#
# A signal is described by a JSON document -- the same one the C++ library and
# the `siggen` tool accept -- and then streamed. numpy is used when it is
# installed and is not required: with it, take() fills an array the C side
# writes into directly, without it you get a plain list.
#
# Samples are generated in bulk on the C side on purpose. One FFI call per
# sample would cost far more than generating the sample does, so take(n) is the
# call to reach for and next() is only a convenience.

import ctypes
import ctypes.util
import json
import os
import sys

__all__ = [
    "Signal",
    "SigGenError",
    "plot",
    "unix_now",
    "version",
    "HAVE_NUMPY",
]

try:
    import numpy as _np

    HAVE_NUMPY = True
except ImportError:  # pragma: no cover - exercised only without numpy
    _np = None
    HAVE_NUMPY = False


class SigGenError(Exception):
    """Raised for a malformed document, a bad parameter or a failed call."""


def _candidate_names():
    if sys.platform.startswith("win"):
        return ["siggen_c.dll"]
    if sys.platform == "darwin":
        return ["libsiggen_c.dylib"]
    return ["libsiggen_c.so"]


def _load_library():
    override = os.environ.get("SIGGEN_C_LIBRARY")
    if override:
        return ctypes.CDLL(override)

    here = os.path.dirname(os.path.abspath(__file__))
    for name in _candidate_names():
        path = os.path.join(here, name)
        if os.path.exists(path):
            return ctypes.CDLL(path)

    found = ctypes.util.find_library("siggen_c")
    if found:
        return ctypes.CDLL(found)

    raise OSError(
        "could not locate the siggen_c shared library; set SIGGEN_C_LIBRARY "
        "to its path"
    )


_lib = _load_library()

_DoubleP = ctypes.POINTER(ctypes.c_double)


class _PlotOptions(ctypes.Structure):
    """Mirrors siggen_plot_options_t. Always populated through
    siggen_plot_options_init(), so that a field added on the C side keeps a
    sensible value here rather than whatever was on the stack."""

    _fields_ = [
        ("width", ctypes.c_size_t),
        ("height", ctypes.c_size_t),
        ("ascii", ctypes.c_int),
        ("axes", ctypes.c_int),
        ("has_y_min", ctypes.c_int),
        ("y_min", ctypes.c_double),
        ("has_y_max", ctypes.c_int),
        ("y_max", ctypes.c_double),
        ("label_width", ctypes.c_size_t),
        ("x_unit", ctypes.c_char_p),
    ]


_PlotOptionsP = ctypes.POINTER(_PlotOptions)

_lib.siggen_create.restype = ctypes.c_void_p
_lib.siggen_create.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]

_lib.siggen_create_from_file.restype = ctypes.c_void_p
_lib.siggen_create_from_file.argtypes = [
    ctypes.c_char_p,
    ctypes.POINTER(ctypes.c_void_p),
]

_lib.siggen_clone.restype = ctypes.c_void_p
_lib.siggen_clone.argtypes = [ctypes.c_void_p]

_lib.siggen_destroy.restype = None
_lib.siggen_destroy.argtypes = [ctypes.c_void_p]

_lib.siggen_last_error.restype = ctypes.c_char_p
_lib.siggen_last_error.argtypes = [ctypes.c_void_p]

_lib.siggen_free_string.restype = None
_lib.siggen_free_string.argtypes = [ctypes.c_void_p]

_lib.siggen_take.restype = ctypes.c_int
_lib.siggen_take.argtypes = [ctypes.c_void_p, _DoubleP, ctypes.c_size_t]

_lib.siggen_take_series.restype = ctypes.c_int
_lib.siggen_take_series.argtypes = [
    ctypes.c_void_p,
    _DoubleP,
    _DoubleP,
    ctypes.c_size_t,
]

_lib.siggen_next.restype = ctypes.c_double
_lib.siggen_next.argtypes = [ctypes.c_void_p]

_lib.siggen_reset.restype = ctypes.c_int
_lib.siggen_reset.argtypes = [ctypes.c_void_p]

_lib.siggen_set_sample_rate.restype = ctypes.c_int
_lib.siggen_set_sample_rate.argtypes = [ctypes.c_void_p, ctypes.c_double]

_lib.siggen_sample_rate.restype = ctypes.c_double
_lib.siggen_sample_rate.argtypes = [ctypes.c_void_p]

_lib.siggen_set_seed.restype = ctypes.c_int
_lib.siggen_set_seed.argtypes = [ctypes.c_void_p, ctypes.c_uint64]

_lib.siggen_seed.restype = ctypes.c_uint64
_lib.siggen_seed.argtypes = [ctypes.c_void_p]

_lib.siggen_set_snr_db.restype = ctypes.c_int
_lib.siggen_set_snr_db.argtypes = [ctypes.c_void_p, ctypes.c_double]

_lib.siggen_set_noiseless.restype = ctypes.c_int
_lib.siggen_set_noiseless.argtypes = [ctypes.c_void_p]

_lib.siggen_snr_db.restype = ctypes.c_int
_lib.siggen_snr_db.argtypes = [ctypes.c_void_p, _DoubleP]

_lib.siggen_type.restype = ctypes.c_char_p
_lib.siggen_type.argtypes = [ctypes.c_void_p]

_lib.siggen_rms.restype = ctypes.c_double
_lib.siggen_rms.argtypes = [ctypes.c_void_p]

_lib.siggen_time.restype = ctypes.c_double
_lib.siggen_time.argtypes = [ctypes.c_void_p]

_lib.siggen_time_step.restype = ctypes.c_double
_lib.siggen_time_step.argtypes = [ctypes.c_void_p]

_lib.siggen_plot_options_init.restype = None
_lib.siggen_plot_options_init.argtypes = [_PlotOptionsP]

_lib.siggen_plot.restype = ctypes.c_void_p
_lib.siggen_plot.argtypes = [_DoubleP, _DoubleP, ctypes.c_size_t, _PlotOptionsP]

_lib.siggen_plot_values.restype = ctypes.c_void_p
_lib.siggen_plot_values.argtypes = [
    _DoubleP,
    ctypes.c_size_t,
    ctypes.c_double,
    _PlotOptionsP,
]

_lib.siggen_set_epoch.restype = ctypes.c_int
_lib.siggen_set_epoch.argtypes = [ctypes.c_void_p, ctypes.c_double]

_lib.siggen_epoch.restype = ctypes.c_double
_lib.siggen_epoch.argtypes = [ctypes.c_void_p]

_lib.siggen_index_at.restype = ctypes.c_int
_lib.siggen_index_at.argtypes = [
    ctypes.c_void_p,
    ctypes.c_double,
    ctypes.POINTER(ctypes.c_uint64),
]

_lib.siggen_time_at.restype = ctypes.c_double
_lib.siggen_time_at.argtypes = [ctypes.c_void_p, ctypes.c_uint64]

_lib.siggen_is_addressable.restype = ctypes.c_int
_lib.siggen_is_addressable.argtypes = [ctypes.c_void_p]

_lib.siggen_at.restype = ctypes.c_int
_lib.siggen_at.argtypes = [ctypes.c_void_p, ctypes.c_uint64, _DoubleP]

_lib.siggen_values_at.restype = ctypes.c_int
_lib.siggen_values_at.argtypes = [
    ctypes.c_void_p,
    ctypes.c_uint64,
    _DoubleP,
    ctypes.c_size_t,
]

_lib.siggen_unix_now.restype = ctypes.c_double
_lib.siggen_unix_now.argtypes = []

_lib.siggen_version.restype = ctypes.c_char_p
_lib.siggen_version.argtypes = []


def _take_cstr(voidp):
    """Copy the C string out of a c_void_p and free the native buffer."""
    text = ctypes.cast(voidp, ctypes.c_char_p).value
    _lib.siggen_free_string(voidp)
    return text.decode("utf-8") if text is not None else ""


def _new_buffer(n):
    """An n-element float64 buffer plus a pointer the C side can write into,
    using numpy when it is available so that the result needs no copy."""
    if HAVE_NUMPY:
        array = _np.empty(n, dtype=_np.float64)
        return array, array.ctypes.data_as(_DoubleP)
    buffer = (ctypes.c_double * n)()
    return buffer, ctypes.cast(buffer, _DoubleP)


def _finish_buffer(buffer):
    return buffer if HAVE_NUMPY else list(buffer)


def _writable_pointer(out, n):
    """Validate a caller-supplied output array and return a pointer into it."""
    if not HAVE_NUMPY:
        raise SigGenError("the `out` argument needs numpy")
    if not isinstance(out, _np.ndarray):
        raise TypeError("`out` must be a numpy array")
    if out.dtype != _np.float64:
        raise TypeError("`out` must have dtype float64")
    if not out.flags["C_CONTIGUOUS"]:
        raise ValueError("`out` must be C-contiguous")
    if out.size < n:
        raise ValueError(f"`out` holds {out.size} samples, need {n}")
    return out.ctypes.data_as(_DoubleP)


def _read_only_pointer(values):
    """A float64 pointer to `values`, copying only when it has to."""
    if HAVE_NUMPY:
        array = _np.ascontiguousarray(values, dtype=_np.float64)
        return array, array.ctypes.data_as(_DoubleP), array.size
    buffer = (ctypes.c_double * len(values))(*[float(v) for v in values])
    return buffer, ctypes.cast(buffer, _DoubleP), len(values)


def _build_plot_options(options):
    """Turn keyword arguments into a siggen_plot_options_t, starting from the
    library's own defaults. Returns the struct and the encoded x_unit, which
    the caller must keep alive for as long as the struct is in use."""
    struct = _PlotOptions()
    _lib.siggen_plot_options_init(ctypes.byref(struct))

    unknown = set(options) - {
        "width",
        "height",
        "ascii",
        "axes",
        "y_min",
        "y_max",
        "label_width",
        "x_unit",
    }
    if unknown:
        raise TypeError(f"unknown plot option(s): {', '.join(sorted(unknown))}")

    if "width" in options:
        struct.width = int(options["width"])
    if "height" in options:
        struct.height = int(options["height"])
    if "ascii" in options:
        struct.ascii = 1 if options["ascii"] else 0
    if "axes" in options:
        struct.axes = 1 if options["axes"] else 0
    if "label_width" in options:
        struct.label_width = int(options["label_width"])
    if options.get("y_min") is not None:
        struct.has_y_min = 1
        struct.y_min = float(options["y_min"])
    if options.get("y_max") is not None:
        struct.has_y_max = 1
        struct.y_max = float(options["y_max"])

    unit = None
    if options.get("x_unit") is not None:
        unit = options["x_unit"].encode("utf-8")
        struct.x_unit = unit
    return struct, unit


class Signal:
    """A signal generator: build it from a JSON description, then stream it.

    >>> with Signal({"type": "sine", "frequency": 50}, sample_rate=1000) as s:
    ...     samples = s.take(500)

    The description accepts anything the C++ SigGen::from_json() does, so every
    waveform, noise colour, ARIMA process, table and composite is reachable,
    and algebraic expressions in the parameters are evaluated on the way in.
    """

    def __init__(self, config, sample_rate=None, seed=None, epoch=None):
        text = config if isinstance(config, str) else json.dumps(config)
        error = ctypes.c_void_p()
        handle = _lib.siggen_create(text.encode("utf-8"), ctypes.byref(error))
        if not handle:
            raise SigGenError(_take_cstr(error) or "could not build the signal")
        self._handle = handle
        if sample_rate is not None:
            self.sample_rate = sample_rate
        if epoch is not None:
            self.epoch = epoch
        if seed is not None:
            self.seed = seed

    @classmethod
    def from_file(cls, path, sample_rate=None, seed=None, epoch=None):
        """Build a signal from a JSON file."""
        error = ctypes.c_void_p()
        handle = _lib.siggen_create_from_file(
            str(path).encode("utf-8"), ctypes.byref(error)
        )
        if not handle:
            raise SigGenError(_take_cstr(error) or "could not read the file")
        return cls._adopt(handle, sample_rate, seed, epoch)

    @classmethod
    def _adopt(cls, handle, sample_rate=None, seed=None, epoch=None):
        signal = cls.__new__(cls)
        signal._handle = handle
        if sample_rate is not None:
            signal.sample_rate = sample_rate
        if epoch is not None:
            signal.epoch = epoch
        if seed is not None:
            signal.seed = seed
        return signal

    def close(self):
        """Release the native signal. Safe to call more than once."""
        if getattr(self, "_handle", None):
            _lib.siggen_destroy(self._handle)
            self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    def __del__(self):
        self.close()

    def __repr__(self):
        if not getattr(self, "_handle", None):
            return "<siggen.Signal (closed)>"
        return (
            f"<siggen.Signal {self.type} at {self.sample_rate:g} Hz, "
            f"t={self.time:g} s>"
        )

    def _check(self, code):
        if code != 0:
            raise SigGenError(self.last_error or "the call failed")

    @property
    def last_error(self):
        """The message from the most recent failed call, or an empty string."""
        message = _lib.siggen_last_error(self._handle)
        return message.decode("utf-8") if message else ""

    def take(self, n, out=None):
        """The next `n` samples, advancing the stream.

        Returns a numpy array when numpy is installed and a list otherwise.
        Pass `out` -- a C-contiguous float64 array of at least `n` elements --
        to have them written into a buffer you already own.
        """
        n = int(n)
        if n < 0:
            raise ValueError("n must not be negative")
        if out is not None:
            self._check(_lib.siggen_take(self._handle, _writable_pointer(out, n), n))
            return out
        buffer, pointer = _new_buffer(n)
        self._check(_lib.siggen_take(self._handle, pointer, n))
        return _finish_buffer(buffer)

    def take_series(self, n):
        """The next `n` samples as a (times, values) pair, times in seconds."""
        n = int(n)
        if n < 0:
            raise ValueError("n must not be negative")
        times, times_pointer = _new_buffer(n)
        values, values_pointer = _new_buffer(n)
        self._check(
            _lib.siggen_take_series(self._handle, times_pointer, values_pointer, n)
        )
        return _finish_buffer(times), _finish_buffer(values)

    def at(self, index):
        """The sample belonging to an absolute index, measured from the epoch.

        Independent of where the sequential stream has reached, so this and
        take() may be mixed freely. Two signals built from the same description
        return the same value here for the same index, whenever each was
        started -- which is what puts them in phase across processes or
        machines.

        Rebuilding history makes this expensive for the filtered generators;
        use values_at() for anything but a single sample.
        """
        value = ctypes.c_double()
        self._check(
            _lib.siggen_at(self._handle, ctypes.c_uint64(int(index)),
                           ctypes.cast(ctypes.byref(value), _DoubleP))
        )
        return value.value

    def values_at(self, first, count, out=None):
        """`count` samples starting at absolute index `first`.

        Prefer this to a loop over at(): the filtered generators rebuild their
        history once for the whole block instead of once per sample, some two
        hundred times cheaper, and the results are identical to the last bit.

        Returns a numpy array when numpy is installed and a list otherwise;
        pass `out` to fill a float64 array you already own.
        """
        count = int(count)
        if count < 0:
            raise ValueError("count must not be negative")
        first = ctypes.c_uint64(int(first))
        if out is not None:
            self._check(
                _lib.siggen_values_at(
                    self._handle, first, _writable_pointer(out, count), count
                )
            )
            return out
        buffer, pointer = _new_buffer(count)
        self._check(_lib.siggen_values_at(self._handle, first, pointer, count))
        return _finish_buffer(buffer)

    def index_at(self, unix_seconds):
        """The absolute sample index belonging to a wall-clock instant.

        Deriving the index from the clock rather than counting calls is what
        keeps two machines in step: a late caller skips indices and an early one
        repeats them, but neither drifts.
        """
        index = ctypes.c_uint64()
        self._check(
            _lib.siggen_index_at(
                self._handle, float(unix_seconds), ctypes.byref(index)
            )
        )
        return index.value

    def time_at(self, index):
        """The wall-clock instant an index belongs to, inverting index_at()."""
        return _lib.siggen_time_at(self._handle, ctypes.c_uint64(int(index)))

    @property
    def is_addressable(self):
        """Whether at() works for this signal.

        False only where a sample depends on unboundedly much history: an ARIMA
        process with a nonzero order of integration, or a composite containing
        one.
        """
        return bool(_lib.siggen_is_addressable(self._handle))

    @property
    def epoch(self):
        """The instant index 0 belongs to, in seconds since the Unix epoch.

        Defaults to 0 -- the Unix epoch itself -- so two signals that never
        touch it still agree.
        """
        return _lib.siggen_epoch(self._handle)

    @epoch.setter
    def epoch(self, value):
        self._check(_lib.siggen_set_epoch(self._handle, float(value)))

    def next(self):
        """One sample. Convenient, but take() is far faster in bulk."""
        return _lib.siggen_next(self._handle)

    def reset(self):
        """Rewind: the clock returns to zero and the samples repeat exactly."""
        self._check(_lib.siggen_reset(self._handle))

    def clone(self):
        """An independent copy, at the same point in the stream."""
        handle = _lib.siggen_clone(self._handle)
        if not handle:
            raise SigGenError("could not clone the signal")
        return Signal._adopt(handle)

    def plot(self, n, **options):
        """Take `n` samples and render them as terminal art.

        This advances the stream, exactly as take() does; pass the samples to
        the module-level plot() instead to draw ones you already hold.
        """
        times, values = self.take_series(n)
        return plot(values, t=times, **options)

    @property
    def type(self):
        """The generator's name: "sine", "arima", "composite"..."""
        return _lib.siggen_type(self._handle).decode("utf-8")

    @property
    def rms(self):
        """Analytic RMS of the clean waveform, excluding offset and noise."""
        return _lib.siggen_rms(self._handle)

    @property
    def time(self):
        """Timestamp of the next sample, in seconds."""
        return _lib.siggen_time(self._handle)

    @property
    def time_step(self):
        """The sampling period, in seconds."""
        return _lib.siggen_time_step(self._handle)

    @property
    def sample_rate(self):
        """Sampling frequency in hertz."""
        return _lib.siggen_sample_rate(self._handle)

    @sample_rate.setter
    def sample_rate(self, value):
        self._check(_lib.siggen_set_sample_rate(self._handle, float(value)))

    @property
    def seed(self):
        """Seed of the random stream. Setting it also rewinds the signal."""
        return _lib.siggen_seed(self._handle)

    @seed.setter
    def seed(self, value):
        self._check(_lib.siggen_set_seed(self._handle, ctypes.c_uint64(int(value))))

    @property
    def snr_db(self):
        """Signal-to-noise ratio of the intrinsic white noise, in decibels,
        or None when the signal is noiseless. Assign None to switch it off."""
        value = ctypes.c_double()
        if _lib.siggen_snr_db(self._handle, ctypes.byref(value)):
            return value.value
        return None

    @snr_db.setter
    def snr_db(self, value):
        if value is None:
            self._check(_lib.siggen_set_noiseless(self._handle))
        else:
            self._check(_lib.siggen_set_snr_db(self._handle, float(value)))


def plot(y, t=None, time_step=1.0, **options):
    """Render a series as terminal art, returning a newline-terminated string.

    Pass `t` for explicit timestamps, or `time_step` for evenly spaced ones.
    Options: width, height, ascii, axes, y_min, y_max, label_width, x_unit.
    """
    values, values_pointer, n = _read_only_pointer(y)
    struct, _unit = _build_plot_options(options)

    if t is not None:
        times, times_pointer, count = _read_only_pointer(t)
        if count != n:
            raise ValueError(f"t has {count} points, y has {n}")
        result = _lib.siggen_plot(
            times_pointer, values_pointer, n, ctypes.byref(struct)
        )
    else:
        result = _lib.siggen_plot_values(
            values_pointer, n, float(time_step), ctypes.byref(struct)
        )
    if not result:
        raise SigGenError("could not render the plot")
    return _take_cstr(result)


def unix_now():
    """Seconds since the Unix epoch, from the system clock.

    The system clock, not a steady one: only wall-clock time means the same
    thing on two machines. Feed it to Signal.index_at() to drive real-time
    generation.
    """
    return _lib.siggen_unix_now()


def version():
    """The SigGen version the shared library was built from."""
    return _lib.siggen_version().decode("utf-8")
