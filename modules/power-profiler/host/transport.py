"""
transport.py — binary serial protocol parser for the power profiler firmware.

Wire format (5 bytes per sample):
  byte 0: 'S'  (0x53) — sample marker
  byte 1: current LSB  (0.1 mA units)
  byte 2: current MSB
  byte 3: annotation bitmask
  byte 4: checksum = 0xFF ^ byte1 ^ byte2 ^ byte3

Special single-byte messages:
  'O'  (0x4F) — overflow: one or more samples were dropped
  'R'  (0x52) — rate packet (3 bytes total): 'R' | rate_lo | rate_hi

The parser is a byte-at-a-time state machine that re-synchronises on
checksum failures without dropping subsequent valid packets.
"""

from __future__ import annotations

import threading
import time
from dataclasses import dataclass, field
from typing import Callable, Optional

import serial


@dataclass
class Sample:
    time_s:      float    # seconds from capture start
    current_mA:  float    # milliamps (positive = forward current)
    ann_mask:    int       # annotation bitmask (bits 0–3)


SampleCallback   = Callable[[Sample], None]
OverflowCallback = Callable[[], None]


class Profiler:
    """
    Opens a serial connection to the profiler firmware, negotiates sample rate
    and runs a background reader thread.

    Callbacks are called from the reader thread — use thread-safe data
    structures on the caller side.
    """

    PKT_SAMPLE = 0x53  # 'S'
    PKT_STOP   = ord('>')
    PKT_START  = ord('<')
    PKT_IDENT  = ord('I')
    PKT_RATE   = ord('R')
    PKT_OVER   = ord('O')

    def __init__(self, port: str, baud: int = 1_000_000) -> None:
        self._ser          = serial.Serial(port, baud, timeout=1.0)
        self._sample_rate  = 1000        # updated after identify
        self._t0           = 0.0
        self._sample_count = 0
        self._overflow_count = 0
        self._running      = False

        self._sample_cbs:   list[SampleCallback]   = []
        self._overflow_cbs: list[OverflowCallback] = []

        self._thread: Optional[threading.Thread] = None

    # ── Public API ─────────────────────────────────────────────────────────────

    def on_sample(self, cb: SampleCallback) -> None:
        self._sample_cbs.append(cb)

    def on_overflow(self, cb: OverflowCallback) -> None:
        self._overflow_cbs.append(cb)

    def identify(self) -> int:
        """Query firmware for sample rate. Returns sample rate in Hz."""
        self._ser.reset_input_buffer()
        self._ser.write(bytes([self.PKT_IDENT]))
        self._ser.flush()
        # Wait for 'R' + 2 bytes.
        deadline = time.monotonic() + 2.0
        while time.monotonic() < deadline:
            if self._ser.in_waiting >= 3:
                hdr = self._ser.read(1)
                if hdr[0] == self.PKT_RATE:
                    lo = self._ser.read(1)[0]
                    hi = self._ser.read(1)[0]
                    self._sample_rate = lo | (hi << 8)
                    return self._sample_rate
        raise TimeoutError("Firmware did not respond to identify")

    @property
    def sample_rate(self) -> int:
        return self._sample_rate

    @property
    def overflow_count(self) -> int:
        return self._overflow_count

    def start(self) -> None:
        """Begin capture. Starts background reader thread."""
        self._sample_count   = 0
        self._overflow_count = 0
        self._t0             = time.monotonic()
        self._running        = True
        self._ser.reset_input_buffer()
        self._ser.write(bytes([self.PKT_START]))
        self._ser.flush()
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        """Stop capture and join the reader thread."""
        self._ser.write(bytes([self.PKT_STOP]))
        self._ser.flush()
        self._running = False
        if self._thread:
            self._thread.join(timeout=2.0)

    def close(self) -> None:
        self.stop()
        self._ser.close()

    def __enter__(self) -> "Profiler":
        return self

    def __exit__(self, *_) -> None:
        self.close()

    # ── Background reader ──────────────────────────────────────────────────────

    def _reader(self) -> None:
        """
        Byte-at-a-time state machine.
        States: HUNT (looking for 'S') → PAYLOAD (reading 4 bytes) → verify.
        """
        STATE_HUNT    = 0
        STATE_PAYLOAD = 1

        state   = STATE_HUNT
        payload = bytearray(4)
        p_idx   = 0

        while self._running:
            raw = self._ser.read(1)
            if not raw:
                continue
            b = raw[0]

            if state == STATE_HUNT:
                if b == self.PKT_SAMPLE:
                    state = STATE_PAYLOAD
                    p_idx = 0
                elif b == self.PKT_OVER:
                    self._overflow_count += 1
                    for cb in self._overflow_cbs:
                        cb()
                # Any other byte is ignored in HUNT state (re-sync).

            elif state == STATE_PAYLOAD:
                payload[p_idx] = b
                p_idx += 1
                if p_idx == 4:
                    state = STATE_HUNT
                    self._process_payload(bytes(payload))

    def _process_payload(self, payload: bytes) -> None:
        lo, hi, ann, csum = payload
        expected = 0xFF ^ lo ^ hi ^ ann
        if csum != expected:
            # Checksum failure — re-enter HUNT state (handled by caller).
            return

        raw_0p1mA   = lo | (hi << 8)
        current_mA  = raw_0p1mA / 10.0
        t           = time.monotonic() - self._t0

        sample = Sample(
            time_s     = t,
            current_mA = current_mA,
            ann_mask   = ann,
        )
        self._sample_count += 1
        for cb in self._sample_cbs:
            cb(sample)
