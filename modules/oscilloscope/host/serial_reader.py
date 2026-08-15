"""Background thread that reads and parses binary frames from the Pico.

The Pico streams frames continuously once CMD_START is sent.  This thread
reads from the serial port, hunts for the 4-byte sync word, parses the
header and payload, verifies the CRC-16 and puts validated Frame objects
on a queue for the GUI to consume.

Frame format (all little-endian):
    [4B sync][1B channel][1B flags][2B n_samples][n_samples×2B uint16][2B CRC16]

Dropped frames (bad CRC, truncated read) increment the drop_count counter but
do not raise exceptions — the reader resynchronises automatically by scanning
for the next sync word.
"""

from __future__ import annotations

import queue
import struct
import threading
import time
from dataclasses import dataclass, field

import numpy as np
import serial

from protocol import (
    FRAME_SYNC, FRAME_HDR_LEN, FRAME_FTR_LEN,
    FRAME_FLAG_OVERFLOW, ADC_FULL_SCALE, ADC_VREF_MV,
    crc16, cmd_ping, PONG_BYTE,
)


@dataclass
class Frame:
    channel:  int
    overflow: bool
    samples:  np.ndarray    # float32, millivolts, shape (n,)
    raw:      np.ndarray    # uint16, 12-bit values, shape (n,)
    seq:      int           # monotonic frame counter


class SerialReader:
    """
    Usage::

        reader = SerialReader("/dev/ttyACM0", baud=115200)
        reader.start()
        frame = reader.get_frame(timeout=0.5)
        reader.stop()
    """

    def __init__(self, port: str, baud: int = 115200, maxqueue: int = 8) -> None:
        self._port     = port
        self._baud     = baud
        self._q: queue.Queue[Frame] = queue.Queue(maxsize=maxqueue)
        self._stop_evt = threading.Event()
        self._thread   = threading.Thread(target=self._run, daemon=True)
        self.drop_count   = 0
        self.frame_count  = 0
        self._ser: serial.Serial | None = None

    # ── Public API ─────────────────────────────────────────────────────────────

    def start(self) -> None:
        self._stop_evt.clear()
        self._thread.start()

    def stop(self) -> None:
        self._stop_evt.set()
        self._thread.join(timeout=2.0)
        if self._ser and self._ser.is_open:
            self._ser.close()

    def send(self, data: bytes) -> None:
        if self._ser and self._ser.is_open:
            self._ser.write(data)

    def get_frame(self, timeout: float = 0.1) -> Frame | None:
        try:
            return self._q.get(timeout=timeout)
        except queue.Empty:
            return None

    def ping(self, timeout: float = 0.5) -> bool:
        """Send a ping and wait for the pong response."""
        if not (self._ser and self._ser.is_open):
            return False
        self._ser.write(cmd_ping())
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self._ser.in_waiting and self._ser.read(1)[0] == PONG_BYTE:
                self._ser.read(4)   # consume 4-byte version
                return True
            time.sleep(0.01)
        return False

    # ── Background thread ──────────────────────────────────────────────────────

    def _run(self) -> None:
        try:
            self._ser = serial.Serial(self._port, self._baud, timeout=0.1)
        except serial.SerialException as exc:
            print(f"[SerialReader] Cannot open {self._port}: {exc}")
            return

        buf = bytearray()

        while not self._stop_evt.is_set():
            chunk = self._ser.read(4096)
            if chunk:
                buf.extend(chunk)
            buf = self._parse_buffer(buf)

        self._ser.close()

    def _parse_buffer(self, buf: bytearray) -> bytearray:
        while True:
            # Hunt for 4-byte sync word
            idx = buf.find(FRAME_SYNC)
            if idx < 0:
                # Keep the last 3 bytes in case a sync spans two reads
                return buf[-3:] if len(buf) >= 3 else buf
            if idx > 0:
                self.drop_count += idx    # bytes skipped before sync
                buf = buf[idx:]

            # Need at least header
            if len(buf) < FRAME_HDR_LEN:
                return buf

            # Parse header
            channel  = buf[4]
            flags    = buf[5]
            n_samples, = struct.unpack_from("<H", buf, 6)

            frame_len = FRAME_HDR_LEN + n_samples * 2 + FRAME_FTR_LEN
            if len(buf) < frame_len:
                return buf   # wait for more data

            payload = bytes(buf[:frame_len])
            crc_recv, = struct.unpack_from("<H", payload, frame_len - 2)
            crc_calc  = crc16(payload[: frame_len - 2])

            if crc_recv != crc_calc:
                self.drop_count += 1
                buf = buf[1:]   # skip one byte and resync
                continue

            # Valid frame
            raw_u16 = np.frombuffer(payload, dtype="<u2",
                                    count=n_samples,
                                    offset=FRAME_HDR_LEN)
            # Firmware stores 12-bit value left-shifted into uint16 [15:4]
            raw12 = (raw_u16 >> 4).astype(np.uint16)
            mv    = raw12.astype(np.float32) * (ADC_VREF_MV / ADC_FULL_SCALE)

            frame = Frame(
                channel  = channel & 0x03,
                overflow = bool(flags & FRAME_FLAG_OVERFLOW),
                samples  = mv,
                raw      = raw12,
                seq      = self.frame_count,
            )
            self.frame_count += 1

            try:
                self._q.put_nowait(frame)
            except queue.Full:
                self._q.get_nowait()    # drop oldest frame
                self._q.put_nowait(frame)

            buf = buf[frame_len:]
