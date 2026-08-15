"""
Serial transport for the mesh base node.

Parses newline-terminated ASCII packets from the base node firmware:

    DATA,<origin>,<hops>,<temp_x10>,<hum_x10>,<light_adc>,<motion>

Delivers parsed NodeReading objects to registered callbacks.
Non-DATA lines (READY, ERR, …) are passed to a log callback.
"""

from __future__ import annotations

import threading
import time
from dataclasses import dataclass, field
from typing import Callable, Optional

import serial


@dataclass
class NodeReading:
    node_id:     int
    hops:        int
    temperature: float        # °C; None if DHT22 failed
    humidity:    float        # %; None if DHT22 failed
    light_adc:   int          # 0–1023 raw ADC
    motion:      bool
    timestamp:   float = field(default_factory=time.monotonic)

    @property
    def light_pct(self) -> float:
        """Light level as a percentage of full-scale ADC."""
        return self.light_adc / 1023.0 * 100.0


ReadingCallback = Callable[[NodeReading], None]
LogCallback     = Callable[[str], None]


class BaseTransport:
    """
    Opens a serial connection to the base node and runs a background reader
    thread that delivers NodeReading objects to registered callbacks.
    """

    def __init__(self, port: str, baud: int = 115200) -> None:
        self._ser     = serial.Serial(port, baud, timeout=1.0)
        self._reading_cbs: list[ReadingCallback] = []
        self._log_cbs:     list[LogCallback]     = []
        self._running = True
        self._thread  = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()
        time.sleep(1.5)  # let the base node boot

    def on_reading(self, cb: ReadingCallback) -> None:
        self._reading_cbs.append(cb)

    def on_log(self, cb: LogCallback) -> None:
        self._log_cbs.append(cb)

    def close(self) -> None:
        self._running = False
        self._ser.close()

    def __enter__(self) -> "BaseTransport":
        return self

    def __exit__(self, *_) -> None:
        self.close()

    # ── Background reader ─────────────────────────────────────────────────────

    def _reader(self) -> None:
        while self._running:
            try:
                raw = self._ser.readline()
            except Exception:
                break
            if not raw:
                continue
            line = raw.decode(errors="replace").strip()
            if not line:
                continue
            if line.startswith("DATA,"):
                r = self._parse_data(line)
                if r:
                    for cb in self._reading_cbs:
                        cb(r)
            else:
                for cb in self._log_cbs:
                    cb(line)

    @staticmethod
    def _parse_data(line: str) -> Optional[NodeReading]:
        # Format: DATA,<origin>,<hops>,<temp_x10>,<hum_x10>,<light>,<motion>
        parts = line.split(",")
        if len(parts) != 7:
            return None
        try:
            origin    = int(parts[1])
            hops      = int(parts[2])
            temp_x10  = int(parts[3])
            hum_x10   = int(parts[4])
            light_adc = int(parts[5])
            motion    = bool(int(parts[6]))
        except (ValueError, IndexError):
            return None

        temp = None if temp_x10 == -999 else temp_x10 / 10.0
        hum  = None if hum_x10 == 0xFFFF else hum_x10 / 10.0

        return NodeReading(
            node_id=origin,
            hops=hops,
            temperature=temp,
            humidity=hum,
            light_adc=light_adc,
            motion=motion,
        )
