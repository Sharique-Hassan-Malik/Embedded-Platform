"""Serial frame decoder for the LoRa mesh base node.

The base node emits newline-delimited JSON over USB-serial.
This module parses those frames and exposes typed dataclasses.

Frame types:
    DataRecord   — sensor reading from a mesh node
    StatusRecord — base node mesh statistics
    NackRecord   — delivery failure notification
    BootRecord   — boot / ready events
    RawRecord    — unrecognised payload (len < 13)
"""

from __future__ import annotations

import json
import threading
import queue
import time
from dataclasses import dataclass, field
from typing import Union

import serial


# ── Typed frame dataclasses ────────────────────────────────────────────────

@dataclass
class DataRecord:
    src:       int
    rssi:      int     # dBm
    snr:       int     # dB
    hops:      int
    temp_c:    float
    hum_pct:   float
    uptime_s:  int
    rx_time:   float = field(default_factory=time.time)

@dataclass
class StatusRecord:
    addr:          int
    routes:        int
    tx:            int
    rx:            int
    relay:         int
    drop_dup:      int
    drop_ttl:      int
    drop_no_route: int
    rx_time:       float = field(default_factory=time.time)

@dataclass
class NackRecord:
    dest:    int
    seq:     int
    rx_time: float = field(default_factory=time.time)

@dataclass
class BootRecord:
    addr:    int
    ready:   bool
    rx_time: float = field(default_factory=time.time)

@dataclass
class RawRecord:
    src:     int
    length:  int
    rx_time: float = field(default_factory=time.time)

Frame = Union[DataRecord, StatusRecord, NackRecord, BootRecord, RawRecord]


# ── Parser ─────────────────────────────────────────────────────────────────

def parse_line(line: str) -> Frame | None:
    """Parse one JSON line from the base node.  Returns None on parse error."""
    line = line.strip()
    if not line:
        return None
    try:
        obj = json.loads(line)
    except json.JSONDecodeError:
        return None

    t = obj.get("type")
    now = time.time()

    if t == "data":
        return DataRecord(
            src      = obj["src"],
            rssi     = obj["rssi"],
            snr      = obj["snr"],
            hops     = obj["hops"],
            temp_c   = obj["temp_c"],
            hum_pct  = obj["hum_pct"],
            uptime_s = obj["uptime_s"],
            rx_time  = now,
        )
    if t == "status":
        return StatusRecord(
            addr          = obj["addr"],
            routes        = obj["routes"],
            tx            = obj["tx"],
            rx            = obj["rx"],
            relay         = obj["relay"],
            drop_dup      = obj["drop_dup"],
            drop_ttl      = obj["drop_ttl"],
            drop_no_route = obj["drop_no_route"],
            rx_time       = now,
        )
    if t == "nack":
        return NackRecord(dest=obj["dest"], seq=obj["seq"], rx_time=now)
    if t in ("boot", "ready"):
        return BootRecord(addr=obj.get("addr", 0), ready=(t == "ready"), rx_time=now)
    if t == "raw":
        return RawRecord(src=obj.get("src", 0), length=obj.get("len", 0), rx_time=now)
    return None


# ── Background serial reader ───────────────────────────────────────────────

class SerialReader:
    """Reads lines from the base node serial port in a daemon thread.

    Usage::

        reader = SerialReader("/dev/ttyACM0", baud=115200)
        reader.start()
        frame = reader.get(timeout=1.0)
        reader.stop()
    """

    def __init__(self, port: str, baud: int = 115200, maxqueue: int = 256) -> None:
        self._port   = port
        self._baud   = baud
        self._q: queue.Queue[Frame] = queue.Queue(maxsize=maxqueue)
        self._stop   = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self.line_count  = 0
        self.error_count = 0

    def start(self) -> None:
        self._stop.clear()
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=2.0)

    def get(self, timeout: float = 0.1) -> Frame | None:
        try:
            return self._q.get(timeout=timeout)
        except queue.Empty:
            return None

    def _run(self) -> None:
        try:
            ser = serial.Serial(self._port, self._baud, timeout=0.5)
        except serial.SerialException as exc:
            print(f"[SerialReader] {exc}")
            return

        while not self._stop.is_set():
            try:
                raw = ser.readline()
                if not raw:
                    continue
                self.line_count += 1
                frame = parse_line(raw.decode("utf-8", errors="replace"))
                if frame is not None:
                    try:
                        self._q.put_nowait(frame)
                    except queue.Full:
                        self._q.get_nowait()
                        self._q.put_nowait(frame)
                else:
                    self.error_count += 1
            except serial.SerialException:
                break

        ser.close()
