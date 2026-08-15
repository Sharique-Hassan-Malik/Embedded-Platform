"""Signal processing applied to each captured frame before display.

Trigger
-------
Edge trigger (rising or falling) on a configurable threshold level.  The
trigger searches for the first crossing of `level_mv` in the correct
direction within the frame and returns the index.  If no crossing is found
the function returns 0 so the display still updates (free-running mode).

Measurements
------------
All measurement functions operate on millivolt arrays.

Timebase
--------
`timebase_string` converts a duration in seconds to a human-readable string
with appropriate SI prefix (ns, µs, ms, s).
"""

from __future__ import annotations

import numpy as np


# ── Trigger ────────────────────────────────────────────────────────────────────

EDGE_RISING  = "rising"
EDGE_FALLING = "falling"


def find_trigger(samples: np.ndarray, level_mv: float,
                 edge: str = EDGE_RISING,
                 pre_samples: int = 50) -> int:
    """Return the index of the first trigger event in *samples*.

    Parameters
    ----------
    samples:     millivolt waveform
    level_mv:    threshold in millivolts
    edge:        "rising" or "falling"
    pre_samples: number of samples before the trigger point to include

    Returns
    -------
    Start index such that `samples[start:]` begins `pre_samples` before the
    crossing.  Returns 0 if no crossing is found (free-running fallback).
    """
    n = len(samples)
    if n < 2:
        return 0

    above = samples >= level_mv

    if edge == EDGE_RISING:
        # Rising edge: False → True transition
        crossings = np.where(~above[:-1] & above[1:])[0]
    else:
        # Falling edge: True → False transition
        crossings = np.where(above[:-1] & ~above[1:])[0]

    if len(crossings) == 0:
        return 0

    idx = int(crossings[0]) - pre_samples
    return max(0, idx)


# ── Measurements ───────────────────────────────────────────────────────────────

def measure_vmin(samples: np.ndarray) -> float:
    return float(np.min(samples))


def measure_vmax(samples: np.ndarray) -> float:
    return float(np.max(samples))


def measure_vpp(samples: np.ndarray) -> float:
    return float(np.ptp(samples))


def measure_vmean(samples: np.ndarray) -> float:
    return float(np.mean(samples))


def measure_vrms(samples: np.ndarray) -> float:
    mean = np.mean(samples)
    return float(np.sqrt(np.mean((samples - mean) ** 2)))


def measure_frequency(samples: np.ndarray, sample_rate_hz: float) -> float | None:
    """Estimate fundamental frequency via zero-crossing rate.

    Returns None if the signal is too noisy or flat to estimate.
    """
    if sample_rate_hz <= 0 or len(samples) < 4:
        return None

    mean = float(np.mean(samples))
    centred = samples - mean

    # Count zero crossings (sign changes)
    crossings = int(np.sum(np.diff(np.signbit(centred))))
    if crossings < 2:
        return None

    duration_s = len(samples) / sample_rate_hz
    # Each full cycle produces 2 zero crossings
    freq = crossings / (2.0 * duration_s)
    return freq


def measure_duty_cycle(samples: np.ndarray, threshold_mv: float) -> float | None:
    """Fraction of samples above threshold_mv, expressed as a percentage."""
    if len(samples) == 0:
        return None
    return float(100.0 * np.sum(samples >= threshold_mv) / len(samples))


def all_measurements(samples: np.ndarray, sample_rate_hz: float,
                     threshold_mv: float) -> dict[str, float | None]:
    return {
        "Vmin":   measure_vmin(samples),
        "Vmax":   measure_vmax(samples),
        "Vpp":    measure_vpp(samples),
        "Vmean":  measure_vmean(samples),
        "Vrms":   measure_vrms(samples),
        "Freq":   measure_frequency(samples, sample_rate_hz),
        "Duty%":  measure_duty_cycle(samples, threshold_mv),
    }


# ── Timebase helpers ───────────────────────────────────────────────────────────

def timebase_string(seconds_per_div: float) -> str:
    """Human-readable timebase label."""
    if seconds_per_div < 1e-6:
        return f"{seconds_per_div * 1e9:.0f} ns/div"
    if seconds_per_div < 1e-3:
        return f"{seconds_per_div * 1e6:.0f} µs/div"
    if seconds_per_div < 1.0:
        return f"{seconds_per_div * 1e3:.0f} ms/div"
    return f"{seconds_per_div:.2f} s/div"


def voltage_string(mv: float) -> str:
    """Format a millivolt value as V or mV."""
    if abs(mv) >= 1000:
        return f"{mv / 1000:.3f} V"
    return f"{mv:.1f} mV"
