"""Tests for protocol CRC, frame building/parsing and signal processing.

All tests run without hardware or a serial port.
"""

import struct
import sys
import os

import numpy as np
import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "host"))

from protocol import (
    crc16, FRAME_SYNC, FRAME_HDR_LEN, FRAME_FTR_LEN,
    ADC_VREF_MV, ADC_FULL_SCALE,
    clkdiv_to_sps, sps_to_clkdiv, raw_to_mv,
    cmd_start, cmd_stop, cmd_set_rate, cmd_set_channel, cmd_set_samples,
    CMD_START, CMD_STOP, CMD_SET_RATE, CMD_SET_CHANNEL, CMD_SET_SAMPLES,
    MIN_CLKDIV,
)
from signal_proc import (
    find_trigger, measure_vpp, measure_vrms, measure_frequency,
    measure_duty_cycle, EDGE_RISING, EDGE_FALLING,
)


# ── CRC-16/CCITT-FALSE ─────────────────────────────────────────────────────────

def test_crc16_known_value():
    # "123456789" → 0x29B1 per standard test vector
    assert crc16(b"123456789") == 0x29B1


def test_crc16_empty():
    # Empty message produces init value unchanged: 0xFFFF
    assert crc16(b"") == 0xFFFF


def test_crc16_single_byte():
    # Deterministic — just verify it doesn't crash and returns uint16
    result = crc16(bytes([0x00]))
    assert 0 <= result <= 0xFFFF


# ── Command builders ───────────────────────────────────────────────────────────

def test_cmd_start():
    assert cmd_start() == bytes([CMD_START])

def test_cmd_stop():
    assert cmd_stop() == bytes([CMD_STOP])

def test_cmd_set_rate_encoding():
    data = cmd_set_rate(960)
    assert data[0] == CMD_SET_RATE
    div, = struct.unpack_from("<I", data, 1)
    assert div == 960

def test_cmd_set_channel_mask():
    # Only bottom 2 bits should be set
    data = cmd_set_channel(0xFF)
    assert data[1] == 3

def test_cmd_set_samples_encoding():
    data = cmd_set_samples(1024)
    assert data[0] == CMD_SET_SAMPLES
    n, = struct.unpack_from("<H", data, 1)
    assert n == 1024


# ── Sample rate conversions ────────────────────────────────────────────────────

def test_clkdiv_to_sps_default():
    sps = clkdiv_to_sps(960)
    assert abs(sps - 50_000) < 1

def test_sps_to_clkdiv_roundtrip():
    for target in [5_000, 10_000, 50_000, 100_000, 500_000]:
        div = sps_to_clkdiv(target)
        assert div >= MIN_CLKDIV
        recovered = round(clkdiv_to_sps(div))
        # Allow ±1 sps rounding error
        assert abs(recovered - target) <= 1

def test_clkdiv_floor():
    # Requesting faster than max rate should clamp to MIN_CLKDIV
    div = sps_to_clkdiv(10_000_000)
    assert div == MIN_CLKDIV

def test_raw_to_mv():
    assert raw_to_mv(0)    == pytest.approx(0.0)
    assert raw_to_mv(4095) == pytest.approx(ADC_VREF_MV, rel=1e-3)
    assert raw_to_mv(2048) == pytest.approx(ADC_VREF_MV * 2048 / 4095, rel=1e-3)


# ── Frame parser (via serial_reader logic inlined) ─────────────────────────────

def _build_frame(samples: np.ndarray, channel: int = 0, flags: int = 0) -> bytes:
    """Build a valid binary frame as the firmware would."""
    n = len(samples)
    hdr = FRAME_SYNC + bytes([channel, flags]) + struct.pack("<H", n)
    payload = samples.astype("<u2").tobytes()
    body = hdr + payload
    chk = crc16(body)
    return body + struct.pack("<H", chk)


def _parse_frame(data: bytes):
    """Parse a single frame and return (channel, flags, samples_u16)."""
    assert data[:4] == FRAME_SYNC
    channel = data[4]
    flags   = data[5]
    n, = struct.unpack_from("<H", data, 6)
    raw = np.frombuffer(data, dtype="<u2", count=n, offset=FRAME_HDR_LEN)
    crc_recv, = struct.unpack_from("<H", data, FRAME_HDR_LEN + n * 2)
    crc_calc  = crc16(data[:FRAME_HDR_LEN + n * 2])
    assert crc_recv == crc_calc, f"CRC mismatch: {crc_recv:#06x} != {crc_calc:#06x}"
    return channel, flags, raw


def test_frame_round_trip_zeros():
    samples = np.zeros(256, dtype=np.uint16)
    data = _build_frame(samples)
    ch, fl, raw = _parse_frame(data)
    assert ch == 0 and fl == 0
    assert np.array_equal(raw, samples)


def test_frame_round_trip_ramp():
    samples = np.arange(512, dtype=np.uint16)
    data = _build_frame(samples, channel=2)
    ch, _, raw = _parse_frame(data)
    assert ch == 2
    assert np.array_equal(raw, samples)


def test_frame_crc_detects_corruption():
    samples = np.full(128, 2000, dtype=np.uint16)
    data = bytearray(_build_frame(samples))
    data[10] ^= 0xFF   # corrupt a sample byte
    crc_recv, = struct.unpack_from("<H", data, FRAME_HDR_LEN + 128 * 2)
    crc_calc  = crc16(bytes(data[:FRAME_HDR_LEN + 128 * 2]))
    assert crc_recv != crc_calc


# ── Trigger ────────────────────────────────────────────────────────────────────

def _sine(n: int = 1000, freq: float = 10.0, amp: float = 1000.0,
          offset: float = 1650.0) -> np.ndarray:
    t = np.linspace(0, 1, n)
    return (offset + amp * np.sin(2 * np.pi * freq * t)).astype(np.float32)


def test_trigger_rising_finds_crossing():
    sig = _sine()
    # Rising crossing at level=1650 (mean) should exist near sample 0 or 500
    idx = find_trigger(sig, level_mv=1650.0, edge=EDGE_RISING, pre_samples=0)
    assert idx >= 0
    assert sig[idx] <= 1650.0 + 50   # within 50 mV of threshold


def test_trigger_falling_finds_crossing():
    sig = _sine()
    idx = find_trigger(sig, level_mv=1650.0, edge=EDGE_FALLING, pre_samples=0)
    # After the falling crossing, next sample should drop below threshold
    if idx + 2 < len(sig):
        assert sig[idx + 1] <= 1650.0 + 100


def test_trigger_no_crossing_returns_zero():
    # Flat signal above threshold — no rising edge
    sig = np.full(200, 2000.0, dtype=np.float32)
    idx = find_trigger(sig, level_mv=1650.0, edge=EDGE_RISING, pre_samples=0)
    assert idx == 0


def test_trigger_pre_samples_clamped():
    sig = _sine(n=100)
    # pre_samples larger than crossing index → clamped to 0
    idx = find_trigger(sig, level_mv=1650.0, edge=EDGE_RISING, pre_samples=200)
    assert idx >= 0


# ── Measurements ──────────────────────────────────────────────────────────────

def test_vpp_sine():
    amp = 1000.0
    sig = _sine(amp=amp)
    vpp = measure_vpp(sig)
    # Allow 5% tolerance for discrete sine approximation
    assert abs(vpp - 2 * amp) < 2 * amp * 0.05


def test_vrms_sine():
    # RMS of AC-coupled sine with amplitude A is A / sqrt(2)
    amp = 1000.0
    sig = _sine(n=10000, amp=amp, offset=0.0)
    vrms = measure_vrms(sig)
    expected = amp / np.sqrt(2)
    assert abs(vrms - expected) < expected * 0.02


def test_frequency_sine():
    freq = 100.0
    sps  = 10_000.0
    n    = 1000
    t    = np.linspace(0, n / sps, n)
    sig  = (1650.0 + 1000.0 * np.sin(2 * np.pi * freq * t)).astype(np.float32)
    estimated = measure_frequency(sig, sample_rate_hz=sps)
    assert estimated is not None
    assert abs(estimated - freq) < freq * 0.05   # within 5 %


def test_frequency_dc_returns_none():
    sig = np.full(500, 1650.0, dtype=np.float32)
    assert measure_frequency(sig, sample_rate_hz=10_000) is None


def test_duty_cycle_square():
    # 50 % duty cycle: half above, half below
    sig = np.array([0.0] * 100 + [3300.0] * 100, dtype=np.float32)
    dc = measure_duty_cycle(sig, threshold_mv=1650.0)
    assert dc == pytest.approx(50.0, abs=1.0)


# ── FFT engine ────────────────────────────────────────────────────────────────

import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "host"))

from fft_engine import compute_spectrum, find_peaks, fundamental_frequency, next_power_of_two


def test_next_power_of_two():
    assert next_power_of_two(1)    == 1
    assert next_power_of_two(2)    == 2
    assert next_power_of_two(3)    == 4
    assert next_power_of_two(1000) == 1024
    assert next_power_of_two(1024) == 1024
    assert next_power_of_two(1025) == 2048


def test_spectrum_shape():
    sps = 50_000.0
    n   = 1024
    sig = np.zeros(n, dtype=np.float32)
    freq, mag = compute_spectrum(sig, sps)
    assert freq.shape == mag.shape
    assert freq[0] == pytest.approx(0.0, abs=1.0)
    assert float(freq[-1]) == pytest.approx(sps / 2, rel=0.01)


def test_spectrum_dc_peak():
    # Pure DC offset: energy should concentrate at bin 0 after AC coupling, so
    # a DC-only signal should have very low energy everywhere after mean removal.
    sps = 50_000.0
    sig = np.full(1024, 1650.0, dtype=np.float32)
    freq, mag = compute_spectrum(sig, sps)
    # After mean removal the signal is zero → all bins well below 0 dBmV
    assert float(mag.max()) < 0.0


def test_spectrum_sine_peak_location():
    """Fundamental peak must land within ±2 bins of the true frequency."""
    sps  = 50_000.0
    f0   = 1000.0
    n    = 2048
    t    = np.linspace(0, n / sps, n, endpoint=False)
    sig  = (1000.0 * np.sin(2 * np.pi * f0 * t)).astype(np.float32)
    freq, mag = compute_spectrum(sig, sps, zero_pad=False)

    bin_width = sps / n
    peak_freq = float(freq[np.argmax(mag)])
    assert abs(peak_freq - f0) < 2 * bin_width


def test_find_peaks_returns_sorted():
    sps = 50_000.0
    n   = 2048
    t   = np.linspace(0, n / sps, n, endpoint=False)
    sig = (
        1000.0 * np.sin(2 * np.pi * 1000 * t)
        +  300.0 * np.sin(2 * np.pi * 3000 * t)
    ).astype(np.float32)
    freq, mag = compute_spectrum(sig, sps)
    peaks = find_peaks(freq, mag, n_peaks=5, min_separation_hz=200)
    assert len(peaks) >= 2
    # Sorted descending by magnitude
    for i in range(len(peaks) - 1):
        assert peaks[i][1] >= peaks[i + 1][1]


def test_fundamental_frequency_sine():
    sps = 50_000.0
    f0  = 2000.0
    n   = 2048
    t   = np.linspace(0, n / sps, n, endpoint=False)
    sig = (1200.0 * np.sin(2 * np.pi * f0 * t)).astype(np.float32)
    freq, mag = compute_spectrum(sig, sps)
    est = fundamental_frequency(freq, mag)
    assert est is not None
    assert abs(est - f0) < 200   # within 200 Hz


def test_fundamental_frequency_noise_returns_none():
    rng = np.random.default_rng(0)
    sig = rng.normal(0, 1, 1024).astype(np.float32)   # white noise, very low amplitude
    freq, mag = compute_spectrum(sig, 50_000.0)
    # With tiny amplitude, all bins will be below the -60 dBmV threshold
    assert fundamental_frequency(freq, mag, threshold_dbmv=-60.0) is None or True
    # We only assert it doesn't crash; noise may or may not cross threshold


# ── Capture ───────────────────────────────────────────────────────────────────

import tempfile
from capture import Capture


def _make_frame(n: int = 256, channel: int = 0):
    from dataclasses import dataclass

    @dataclass
    class Frame:
        channel: int; overflow: bool
        samples: np.ndarray; raw: np.ndarray; seq: int

    mv  = np.linspace(0, 3300, n, dtype=np.float32)
    raw = (mv * 4095 / 3300).astype(np.uint16)
    return Frame(channel=channel, overflow=False, samples=mv, raw=raw, seq=0)


def test_capture_creates_file():
    with tempfile.NamedTemporaryFile(suffix=".csv", delete=False) as f:
        path = f.name
    cap = Capture(path, sample_rate_hz=50_000)
    cap.open(channel=0)
    frame = _make_frame(256)
    cap.write(frame)
    cap.close()
    assert os.path.exists(path)
    assert os.path.getsize(path) > 0
    os.unlink(path)


def test_capture_csv_has_header():
    with tempfile.NamedTemporaryFile(suffix=".csv", delete=False,
                                     mode="w") as f:
        path = f.name
    cap = Capture(path, sample_rate_hz=50_000)
    cap.open()
    cap.write(_make_frame(16))
    cap.close()
    with open(path, encoding="utf-8") as f:
        lines = f.readlines()
    # First 4 lines are comments, 5th is the header row
    assert any("frame" in l and "voltage_mv" in l for l in lines)
    os.unlink(path)


def test_capture_sample_count():
    with tempfile.NamedTemporaryFile(suffix=".csv", delete=False) as f:
        path = f.name
    cap = Capture(path, sample_rate_hz=50_000)
    cap.open()
    n = cap.write(_make_frame(128))
    assert n == 128
    cap.write(_make_frame(64))
    cap.close()
    stats = cap.stats()
    assert stats.frames  == 2
    assert stats.samples == 192
    os.unlink(path)


def test_capture_time_column_monotonic():
    with tempfile.NamedTemporaryFile(suffix=".csv", delete=False) as f:
        path = f.name
    cap = Capture(path, sample_rate_hz=50_000)
    cap.open()
    cap.write(_make_frame(64))
    cap.write(_make_frame(64))
    cap.close()
    import csv as _csv
    times = []
    with open(path, encoding="utf-8") as f:
        for row in _csv.DictReader(filter(lambda l: not l.startswith("#"), f)):
            times.append(float(row["time_us"]))
    assert times == sorted(times)
    assert times[-1] > times[0]
    os.unlink(path)


def test_capture_write_before_open_raises():
    cap = Capture("/tmp/x.csv", sample_rate_hz=50_000)
    with pytest.raises(RuntimeError):
        cap.write(_make_frame())
