"""
capture.py — in-memory capture buffer and per-annotation statistics.

Accumulates Sample objects from the transport layer and computes summary
statistics per annotation channel (mean, peak, RMS current and energy).
"""

from __future__ import annotations

import math
import threading
from dataclasses import dataclass, field
from typing import Optional

import numpy as np

from transport import Sample


@dataclass
class SectionStats:
    """Statistics for one annotated code section (one annotation channel)."""
    channel:       int
    start_s:       float
    end_s:         Optional[float]
    mean_mA:       float = 0.0
    peak_mA:       float = 0.0
    rms_mA:        float = 0.0
    energy_uJ:     float = 0.0    # µJ = mA × mV_supply × duration_s × 1000
    sample_count:  int   = 0

    @property
    def duration_s(self) -> float:
        if self.end_s is None:
            return 0.0
        return self.end_s - self.start_s


class CaptureSession:
    """
    Thread-safe accumulator for a single capture run.

    Call push() from the transport callback thread.
    Call arrays() or stats() from the main/plot thread.
    """

    ANN_CHANNELS = 4

    def __init__(self, supply_mV: float = 3300.0) -> None:
        self._lock       = threading.Lock()
        self._times:    list[float] = []
        self._currents: list[float] = []
        self._ann:      list[int]   = []
        self._supply_mV = supply_mV

        # Per-channel open section tracker: channel → SectionStats | None
        self._open: list[Optional[SectionStats]] = [None] * self.ANN_CHANNELS
        self._sections: list[SectionStats]       = []
        self._prev_mask = 0

    def push(self, s: Sample) -> None:
        with self._lock:
            self._times.append(s.time_s)
            self._currents.append(s.current_mA)
            self._ann.append(s.ann_mask)
            self._update_annotations(s)

    def _update_annotations(self, s: Sample) -> None:
        """Detect rising/falling edges on each annotation channel."""
        changed = s.ann_mask ^ self._prev_mask
        for ch in range(self.ANN_CHANNELS):
            bit = 1 << ch
            if not (changed & bit):
                continue
            if s.ann_mask & bit:
                # Rising edge: open a new section.
                self._open[ch] = SectionStats(channel=ch, start_s=s.time_s,
                                              end_s=None)
            else:
                # Falling edge: close the section and compute stats.
                sec = self._open[ch]
                if sec is not None:
                    sec.end_s = s.time_s
                    self._compute_stats(sec)
                    self._sections.append(sec)
                    self._open[ch] = None
        self._prev_mask = s.ann_mask

    def _compute_stats(self, sec: SectionStats) -> None:
        """Compute mean, peak, RMS and energy for samples within sec."""
        t_arr = np.asarray(self._times,    dtype=float)
        c_arr = np.asarray(self._currents, dtype=float)
        mask  = (t_arr >= sec.start_s) & (t_arr < sec.end_s)
        vals  = c_arr[mask]
        if len(vals) == 0:
            return
        sec.mean_mA      = float(np.mean(vals))
        sec.peak_mA      = float(np.max(vals))
        sec.rms_mA       = float(np.sqrt(np.mean(vals ** 2)))
        sec.energy_uJ    = float(
            sec.mean_mA * self._supply_mV * sec.duration_s
        )   # mA × mV × s = µW·s = µJ
        sec.sample_count = int(np.sum(mask))

    def arrays(self) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
        """Return (times_s, currents_mA, ann_masks) as numpy arrays."""
        with self._lock:
            t = np.asarray(self._times,    dtype=float)
            c = np.asarray(self._currents, dtype=float)
            a = np.asarray(self._ann,      dtype=np.uint8)
        return t, c, a

    def sections(self) -> list[SectionStats]:
        with self._lock:
            return list(self._sections)

    def clear(self) -> None:
        with self._lock:
            self._times.clear()
            self._currents.clear()
            self._ann.clear()
            self._open    = [None] * self.ANN_CHANNELS
            self._sections.clear()
            self._prev_mask = 0
