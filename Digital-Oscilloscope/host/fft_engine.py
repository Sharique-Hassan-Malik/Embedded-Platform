"""FFT spectrum computation for the oscilloscope.

Computes a single-sided power spectrum from a millivolt waveform using a
Hann window to reduce spectral leakage.  Returns frequency bins, magnitude
in dBm (relative to 1 mW into 50 Ω) and the dominant frequency peak.

The spectrum is padded to the next power of two for efficient FFT computation
and better frequency resolution when the frame is short.

All functions are stateless — call them per-frame from the GUI update loop.
"""

from __future__ import annotations

import numpy as np


def next_power_of_two(n: int) -> int:
    return 1 << (n - 1).bit_length()


def compute_spectrum(
    samples_mv: np.ndarray,
    sample_rate_hz: float,
    zero_pad: bool = True,
) -> tuple[np.ndarray, np.ndarray]:
    """Return (freq_hz, magnitude_dbmv) for a single-sided spectrum.

    Parameters
    ----------
    samples_mv:     millivolt waveform, shape (N,)
    sample_rate_hz: ADC sample rate in Hz
    zero_pad:       pad to next power of two for faster FFT

    Returns
    -------
    freq_hz:        shape (M,) frequency axis in Hz, M = nfft // 2 + 1
    mag_dbmv:       shape (M,) magnitude in dBmV (0 dBmV = 1 mV RMS)
    """
    n = len(samples_mv)
    if n < 4:
        return np.array([0.0]), np.array([-120.0])

    nfft = next_power_of_two(n) if zero_pad else n

    # Hann window — reduces leakage at the cost of slight frequency resolution
    window     = np.hanning(n).astype(np.float32)
    # Coherent power gain correction: sum(window) / n
    window_cg  = window.sum() / n

    windowed   = (samples_mv - samples_mv.mean()) * window   # AC-couple then window
    spectrum   = np.fft.rfft(windowed, n=nfft)

    # Single-sided magnitude, corrected for window and one-sided doubling
    mag_linear = np.abs(spectrum) / (n * window_cg)
    mag_linear[1:-1] *= 2.0   # double non-DC, non-Nyquist bins

    # dBmV: 20 * log10(V_rms / 1 mV)
    mag_linear = np.maximum(mag_linear, 1e-12)   # floor to avoid log(0)
    mag_dbmv   = 20.0 * np.log10(mag_linear)

    freq_hz = np.fft.rfftfreq(nfft, d=1.0 / sample_rate_hz)
    return freq_hz.astype(np.float32), mag_dbmv.astype(np.float32)


def find_peaks(
    freq_hz: np.ndarray,
    mag_dbmv: np.ndarray,
    n_peaks: int = 5,
    min_separation_hz: float = 0.0,
    threshold_dbmv: float = -80.0,
) -> list[tuple[float, float]]:
    """Return up to n_peaks (frequency, magnitude) pairs sorted by magnitude.

    Parameters
    ----------
    freq_hz:            frequency axis from compute_spectrum
    mag_dbmv:           magnitude axis from compute_spectrum
    n_peaks:            maximum number of peaks to return
    min_separation_hz:  minimum Hz gap between reported peaks
    threshold_dbmv:     ignore bins below this level

    Returns
    -------
    List of (freq_hz, mag_dbmv) tuples, highest magnitude first.
    """
    candidates = np.where(mag_dbmv >= threshold_dbmv)[0]
    if len(candidates) == 0:
        return []

    # Simple local-maximum selection
    peaks = []
    for idx in candidates:
        left  = mag_dbmv[idx - 1] if idx > 0              else -np.inf
        right = mag_dbmv[idx + 1] if idx < len(mag_dbmv) - 1 else -np.inf
        if mag_dbmv[idx] >= left and mag_dbmv[idx] >= right:
            peaks.append((float(freq_hz[idx]), float(mag_dbmv[idx])))

    peaks.sort(key=lambda p: p[1], reverse=True)

    # Remove peaks within min_separation_hz of a stronger one
    if min_separation_hz > 0.0:
        kept = []
        for freq, mag in peaks:
            if all(abs(freq - kf) >= min_separation_hz for kf, _ in kept):
                kept.append((freq, mag))
            if len(kept) >= n_peaks:
                break
        return kept

    return peaks[:n_peaks]


def fundamental_frequency(
    freq_hz: np.ndarray,
    mag_dbmv: np.ndarray,
    threshold_dbmv: float = -60.0,
) -> float | None:
    """Return the frequency of the highest-magnitude peak above threshold."""
    peaks = find_peaks(freq_hz, mag_dbmv, n_peaks=1, threshold_dbmv=threshold_dbmv)
    return peaks[0][0] if peaks else None
