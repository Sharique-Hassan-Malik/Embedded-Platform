"""
Real-time telemetry monitor for the FOC motor controller.

Receives 24-byte binary frames over UART and plots:
  - Id and Iq current waveforms
  - Mechanical speed (rad/s and RPM)
  - Rotor position (rad)
  - Mode and fault status

Usage:
    python monitor.py --port /dev/ttyUSB0 --baud 460800
    python monitor.py --file trace.bin           # replay a capture
    python monitor.py --demo                     # simulate a startup trace

Frame format (24 bytes, little-endian):
    [0]      : 0xA5 magic
    [1:5]    : uint32 tick
    [5:9]    : float32 id
    [9:13]   : float32 iq
    [13:17]  : float32 omega
    [17:21]  : float32 theta
    [21]     : uint8  mode
    [22]     : uint8  faults
    [23]     : uint8  XOR checksum (bytes 0–22)
"""

from __future__ import annotations

import argparse
import struct
import time
import threading
import queue
import math
from collections import deque
from dataclasses import dataclass
from typing import Optional

import serial
import matplotlib
matplotlib.use("TkAgg")
import matplotlib.pyplot as plt
import matplotlib.animation as animation
import numpy as np

TELEM_MAGIC  = 0xA5
TELEM_SIZE   = 24
FRAME_FMT    = "<BIfffff BB B"   # magic, tick, id, iq, omega, theta, ?, mode, faults, csum
# Adjust: B I 4f B B B = 1+4+16+1+1+1 = 24 ✓

FRAME_STRUCT = struct.Struct("<B I f f f f B B B")
assert FRAME_STRUCT.size == TELEM_SIZE

MODE_NAMES = {
    0: "IDLE",
    1: "ALIGN",
    2: "OPEN-LOOP",
    3: "CURRENT",
    4: "SPEED",
    5: "POSITION",
    6: "FAULT",
}

FAULT_BITS = {
    0: "OC",
    1: "OV",
    2: "UV",
    3: "OT",
    4: "ENC",
}


@dataclass
class TelemFrame:
    tick:   int
    id_:    float
    iq:     float
    omega:  float
    theta:  float
    mode:   int
    faults: int

    @property
    def rpm(self) -> float:
        return self.omega * 60.0 / (2.0 * math.pi)

    @property
    def mode_name(self) -> str:
        return MODE_NAMES.get(self.mode, f"UNK({self.mode})")

    @property
    def fault_str(self) -> str:
        if self.faults == 0:
            return "OK"
        bits = [name for bit, name in FAULT_BITS.items() if self.faults & (1 << bit)]
        return " | ".join(bits)


def _xor_check(data: bytes) -> bool:
    csum = 0
    for b in data[:TELEM_SIZE - 1]:
        csum ^= b
    return csum == data[TELEM_SIZE - 1]


def parse_frame(data: bytes) -> Optional[TelemFrame]:
    if len(data) < TELEM_SIZE or data[0] != TELEM_MAGIC:
        return None
    if not _xor_check(data):
        return None
    magic, tick, id_, iq, omega, theta, mode, faults, _ = FRAME_STRUCT.unpack(data[:TELEM_SIZE])
    return TelemFrame(tick=tick, id_=id_, iq=iq, omega=omega,
                      theta=theta, mode=mode, faults=faults)


class SerialReader(threading.Thread):
    """Reads UART bytes and emits parsed frames to a queue."""

    def __init__(self, port: str, baud: int, out: queue.Queue) -> None:
        super().__init__(daemon=True)
        self.port = port
        self.baud = baud
        self.out  = out
        self._stop = threading.Event()

    def run(self) -> None:
        try:
            ser = serial.Serial(self.port, self.baud, timeout=0.1)
        except serial.SerialException as e:
            print(f"Serial error: {e}")
            return

        buf = bytearray()
        while not self._stop.is_set():
            data = ser.read(64)
            if not data:
                continue
            buf.extend(data)
            while len(buf) >= TELEM_SIZE:
                idx = buf.find(TELEM_MAGIC)
                if idx < 0:
                    buf.clear()
                    break
                if idx > 0:
                    del buf[:idx]
                if len(buf) < TELEM_SIZE:
                    break
                frame = parse_frame(bytes(buf[:TELEM_SIZE]))
                if frame is not None:
                    self.out.put(frame)
                    del buf[:TELEM_SIZE]
                else:
                    del buf[:1]  # skip bad magic byte and resync

    def stop(self) -> None:
        self._stop.set()


class FileReader(threading.Thread):
    """Replays a binary capture file."""

    def __init__(self, path: str, out: queue.Queue) -> None:
        super().__init__(daemon=True)
        self.path = path
        self.out  = out

    def run(self) -> None:
        with open(self.path, "rb") as f:
            data = f.read()
        i = 0
        while i + TELEM_SIZE <= len(data):
            if data[i] == TELEM_MAGIC:
                frame = parse_frame(data[i:i + TELEM_SIZE])
                if frame is not None:
                    self.out.put(frame)
                    i += TELEM_SIZE
                    time.sleep(0.001)  # 1 kHz playback
                    continue
            i += 1


def generate_demo_frames(out: queue.Queue) -> None:
    """Simulate a motor startup: align → open-loop → speed control."""
    def _run() -> None:
        tick    = 0
        omega   = 0.0
        theta   = 0.0
        target  = 60.0 * 2.0 * math.pi   # 60 rev/s

        for phase, (mode, duration_ms) in enumerate([
            (1, 200),   # ALIGN
            (2, 500),   # OPEN-LOOP
            (4, 3000),  # SPEED
        ]):
            for _ in range(duration_ms):
                if mode == 4:
                    omega += (target - omega) * 0.003
                elif mode == 2:
                    omega += 20.0 * math.pi * 0.001

                theta += omega * 2 * 0.001
                id_   = 0.2 if mode <= 2 else 0.0
                iq    = 0.0 if mode <= 1 else min(2.0, (target - omega) * 0.01)

                csum  = 0
                raw   = FRAME_STRUCT.pack(TELEM_MAGIC, tick, id_, iq,
                                          omega, theta, mode, 0, 0)
                for b in raw[:-1]:
                    csum ^= b
                raw = raw[:-1] + bytes([csum])
                frame = parse_frame(raw)
                if frame:
                    out.put(frame)
                tick += 16
                time.sleep(0.001)

    t = threading.Thread(target=_run, daemon=True)
    t.start()


class Monitor:
    WINDOW = 3000   # samples shown in scroll window

    def __init__(self, frame_q: queue.Queue) -> None:
        self.q = frame_q

        self.times  = deque(maxlen=self.WINDOW)
        self.id_buf = deque(maxlen=self.WINDOW)
        self.iq_buf = deque(maxlen=self.WINDOW)
        self.w_buf  = deque(maxlen=self.WINDOW)
        self.t_buf  = deque(maxlen=self.WINDOW)
        self.t0: Optional[float] = None

        self.fig, axes = plt.subplots(3, 1, figsize=(12, 8), sharex=False)
        self.fig.suptitle("FOC Motor Controller — Live Telemetry", fontsize=13)
        self.ax_i, self.ax_w, self.ax_theta = axes

        self.ax_i.set_ylabel("Current (A)")
        self.ax_i.set_xlabel("Time (s)")
        self.ax_i.set_ylim(-10, 10)
        self.ax_i.grid(True, alpha=0.3)

        self.ax_w.set_ylabel("Speed (rad/s)")
        self.ax_w.set_xlabel("Time (s)")
        self.ax_w.set_ylim(-50, 450)
        self.ax_w.grid(True, alpha=0.3)

        ax2 = self.ax_w.twinx()
        ax2.set_ylabel("RPM", color="grey")
        ax2.set_ylim(-500, 4300)
        ax2.tick_params(axis="y", labelcolor="grey")
        self.ax_w2 = ax2

        self.ax_theta.set_ylabel("Position (rad)")
        self.ax_theta.set_xlabel("Time (s)")
        self.ax_theta.grid(True, alpha=0.3)

        self.line_id,  = self.ax_i.plot([], [], "b-", lw=1.2, label="Id")
        self.line_iq,  = self.ax_i.plot([], [], "r-", lw=1.2, label="Iq")
        self.ax_i.legend(loc="upper right", fontsize=9)

        self.line_w,  = self.ax_w.plot([], [], "g-", lw=1.4, label="ω (rad/s)")
        self.line_w2, = self.ax_w2.plot([], [], "g--", lw=0.8, alpha=0.4, label="RPM")
        self.ax_w.legend(loc="upper left", fontsize=9)

        self.line_th, = self.ax_theta.plot([], [], "m-", lw=1.2, label="θ (rad)")
        self.ax_theta.legend(loc="upper right", fontsize=9)

        self.status_text = self.fig.text(0.01, 0.01, "", fontsize=9,
                                         family="monospace", va="bottom")

        plt.tight_layout(rect=[0, 0.04, 1, 0.96])

    def _drain_queue(self) -> None:
        try:
            while True:
                frame: TelemFrame = self.q.get_nowait()
                t = frame.tick / 16000.0
                if self.t0 is None:
                    self.t0 = t
                t -= self.t0

                self.times.append(t)
                self.id_buf.append(frame.id_)
                self.iq_buf.append(frame.iq)
                self.w_buf.append(frame.omega)
                self.t_buf.append(frame.theta)

                self._last_frame = frame
        except queue.Empty:
            pass

    def _update(self, _frame_num: int) -> list:
        self._drain_queue()
        if not self.times:
            return []

        t = list(self.times)
        self.line_id.set_data(t, list(self.id_buf))
        self.line_iq.set_data(t, list(self.iq_buf))
        self.line_w.set_data(t, list(self.w_buf))
        self.line_w2.set_data(t, [v * 60.0 / (2 * math.pi) for v in self.w_buf])
        self.line_th.set_data(t, list(self.t_buf))

        for ax in (self.ax_i, self.ax_w, self.ax_theta):
            ax.relim()
            ax.autoscale_view(scalex=True, scaley=False)

        if hasattr(self, "_last_frame"):
            f = self._last_frame
            self.status_text.set_text(
                f"Mode: {f.mode_name:<10}  Faults: {f.fault_str:<12}  "
                f"ω = {f.omega:7.2f} rad/s  ({f.rpm:6.1f} RPM)  "
                f"Id = {f.id_:6.3f} A  Iq = {f.iq:6.3f} A  "
                f"θ = {f.theta:6.3f} rad"
            )

        return [self.line_id, self.line_iq, self.line_w, self.line_w2, self.line_th]

    def run(self) -> None:
        self._last_frame = None
        ani = animation.FuncAnimation(
            self.fig, self._update, interval=50, blit=False, cache_frame_data=False
        )
        plt.show()
        del ani


def main() -> None:
    ap = argparse.ArgumentParser(description="FOC motor telemetry monitor")
    src = ap.add_mutually_exclusive_group()
    src.add_argument("--port",  help="Serial port (e.g. /dev/ttyUSB0 or COM3)")
    src.add_argument("--file",  help="Binary capture file to replay")
    src.add_argument("--demo",  action="store_true", help="Run simulated startup demo")
    ap.add_argument("--baud",   type=int, default=460800, help="UART baud rate")
    args = ap.parse_args()

    frame_q: queue.Queue = queue.Queue(maxsize=8192)

    if args.demo or (not args.port and not args.file):
        print("Running demo simulation…")
        generate_demo_frames(frame_q)
    elif args.port:
        reader = SerialReader(args.port, args.baud, frame_q)
        reader.start()
    elif args.file:
        reader = FileReader(args.file, frame_q)
        reader.start()

    monitor = Monitor(frame_q)
    monitor.run()


if __name__ == "__main__":
    main()
