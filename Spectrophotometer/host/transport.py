"""
Low-level serial transport for the spectrophotometer firmware.

Wraps pyserial and exposes one method per firmware command.
All parsing of the ASCII response protocol is done here so higher-level
code never sees raw serial strings.
"""

from __future__ import annotations

import math
import time
from dataclasses import dataclass
from typing import Optional

import serial


@dataclass
class Reading:
    signal: int          # dark-corrected ADC counts
    reference: int       # reference channel ADC counts (0 if not fitted)
    absorbance: float    # Beer-Lambert A; math.nan if no blank stored


class Spectrophotometer:
    """Serial interface to the Arduino spectrophotometer firmware."""

    def __init__(self, port: str, baud: int = 115200, timeout: float = 5.0) -> None:
        self._ser = serial.Serial(port, baud, timeout=timeout)
        time.sleep(2.0)   # wait for Arduino reset on DTR toggle
        self._flush_startup()

    def close(self) -> None:
        self._ser.close()

    def __enter__(self) -> "Spectrophotometer":
        return self

    def __exit__(self, *_) -> None:
        self.close()

    # ── Public commands ───────────────────────────────────────────────────────

    def identify(self) -> str:
        """Return firmware version string."""
        self._send("I")
        return self._readline()

    def status(self) -> dict:
        """Return instrument status as a dict."""
        self._send("S")
        line = self._readline()
        return self._parse_status(line)

    def dark(self) -> int:
        """Measure dark current (LED off). Returns dark ADC count."""
        self._send("D")
        return self._parse_ack("DARK")

    def blank(self) -> int:
        """Store blank (I0). Insert solvent-only cuvette first."""
        self._send("B")
        return self._parse_ack("I0")

    def read(self) -> Reading:
        """Take one averaged measurement. Returns a Reading dataclass."""
        self._send("R")
        return self._parse_data(self._readline())

    def set_led_duty(self, duty: int) -> None:
        """Set LED PWM intensity (0–255)."""
        if not 0 <= duty <= 255:
            raise ValueError(f"duty must be 0–255, got {duty}")
        self._send(f"L{duty:03d}")
        self._parse_ack("DUTY")

    # ── Internal helpers ──────────────────────────────────────────────────────

    def _send(self, cmd: str) -> None:
        self._ser.write((cmd + "\n").encode())
        self._ser.flush()

    def _readline(self) -> str:
        line = self._ser.readline().decode().strip()
        if not line:
            raise TimeoutError("No response from firmware")
        return line

    def _flush_startup(self) -> None:
        """Drain any startup messages (e.g. 'READY') from the serial buffer."""
        deadline = time.monotonic() + 1.0
        while time.monotonic() < deadline:
            if self._ser.in_waiting:
                self._ser.readline()
            else:
                break

    def _parse_ack(self, expected_key: str) -> int:
        line = self._readline()
        # Expected format: "ACK,<key>,<value>"
        parts = line.split(",")
        if len(parts) != 3 or parts[0] != "ACK":
            raise ValueError(f"Unexpected ACK response: {line!r}")
        if parts[1] != expected_key:
            raise ValueError(f"Expected key {expected_key!r}, got {parts[1]!r}")
        return int(parts[2])

    @staticmethod
    def _parse_data(line: str) -> Reading:
        # Expected format: "DATA,<signal>,<reference>,<absorbance>"
        parts = line.split(",")
        if len(parts) != 4 or parts[0] != "DATA":
            raise ValueError(f"Unexpected DATA response: {line!r}")
        signal    = int(parts[1])
        reference = int(parts[2])
        absorbance = math.nan if parts[3] == "nan" else float(parts[3])
        return Reading(signal=signal, reference=reference, absorbance=absorbance)

    @staticmethod
    def _parse_status(line: str) -> dict:
        # Expected format: "STATUS,cuvette=<0|1>,duty=<n>,I0=<n>,dark=<n>"
        if not line.startswith("STATUS,"):
            raise ValueError(f"Unexpected STATUS response: {line!r}")
        result = {}
        for token in line[len("STATUS,"):].split(","):
            k, v = token.split("=")
            result[k] = int(v)
        return result
