"""CSV capture — records waveform frames to disk.

Usage::

    cap = Capture("/tmp/capture.csv", sample_rate_hz=50_000)
    cap.open()
    cap.write(frame)     # call from GUI update loop
    cap.close()
    stats = cap.stats()  # {'frames': 12, 'samples': 12288, 'duration_s': 0.24}

The CSV format is::

    # digital-oscilloscope capture
    # sample_rate_hz=50000.0
    # channel=0
    # started=2025-01-01T12:00:00
    frame,sample,time_us,voltage_mv,raw_adc
    0,0,0.00,1650.3,2047
    0,1,20.00,1651.1,2048
    ...

Times are in microseconds from the start of the capture.
Each frame is written as a contiguous block; the frame index increments
per write call so gaps in streaming are visible in the data.
"""

from __future__ import annotations

import csv
import io
import os
from datetime import datetime
from dataclasses import dataclass
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    pass


@dataclass
class CaptureStats:
    frames:     int
    samples:    int
    duration_s: float
    path:       str
    size_bytes: int


class Capture:
    def __init__(self, path: str, sample_rate_hz: float) -> None:
        self._path          = path
        self._sps           = sample_rate_hz
        self._file: io.TextIOWrapper | None = None
        self._writer: csv.writer | None     = None
        self._frame_idx     = 0
        self._total_samples = 0
        self._start_time_us = 0.0
        self._elapsed_us    = 0.0

    # ── Lifecycle ──────────────────────────────────────────────────────────────

    def open(self, channel: int = 0) -> None:
        """Open (or overwrite) the output file and write the header."""
        os.makedirs(os.path.dirname(os.path.abspath(self._path)), exist_ok=True)
        self._file = open(self._path, "w", newline="", encoding="utf-8")
        self._frame_idx     = 0
        self._total_samples = 0
        self._elapsed_us    = 0.0

        self._file.write(f"# digital-oscilloscope capture\n")
        self._file.write(f"# sample_rate_hz={self._sps:.1f}\n")
        self._file.write(f"# channel={channel}\n")
        self._file.write(f"# started={datetime.now().isoformat(timespec='seconds')}\n")

        self._writer = csv.writer(self._file)
        self._writer.writerow(["frame", "sample", "time_us", "voltage_mv", "raw_adc"])

    def write(self, frame) -> int:
        """Write one Frame to the CSV.  Returns the number of samples written."""
        if self._writer is None:
            raise RuntimeError("Capture.open() must be called before write()")

        n       = len(frame.samples)
        step_us = 1_000_000.0 / self._sps

        for i in range(n):
            t_us = self._elapsed_us + i * step_us
            mv   = float(frame.samples[i])
            raw  = int(frame.raw[i])
            self._writer.writerow([self._frame_idx, i, f"{t_us:.2f}", f"{mv:.2f}", raw])

        self._elapsed_us   += n * step_us
        self._total_samples += n
        self._frame_idx    += 1
        return n

    def flush(self) -> None:
        if self._file:
            self._file.flush()

    def close(self) -> None:
        if self._file:
            self._file.close()
            self._file   = None
            self._writer = None

    def is_open(self) -> bool:
        return self._file is not None

    # ── Stats ──────────────────────────────────────────────────────────────────

    def stats(self) -> CaptureStats:
        size = os.path.getsize(self._path) if os.path.exists(self._path) else 0
        return CaptureStats(
            frames     = self._frame_idx,
            samples    = self._total_samples,
            duration_s = self._elapsed_us / 1_000_000.0,
            path       = self._path,
            size_bytes = size,
        )
