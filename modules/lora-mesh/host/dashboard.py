"""LoRa Mesh Dashboard — real-time terminal display.

Renders a live updating table of all nodes seen by the base node, showing
temperature, humidity, RSSI, SNR, hop count, packet rate and packet loss.

Usage:
    python dashboard.py --port /dev/ttyACM0
    python dashboard.py --demo            # synthetic frames, no hardware
    python dashboard.py --port COM4 --baud 115200 --log captures/run.csv

Keys:
    q   quit
    r   reset statistics
    d   dump routing table request (prints raw JSON to log)
"""

from __future__ import annotations

import argparse
import curses
import math
import queue
import sys
import threading
import time
from collections import defaultdict
from dataclasses import dataclass, field
from datetime import datetime
from typing import Optional

from protocol import DataRecord, StatusRecord, NackRecord, Frame, parse_line


# ── Per-node statistics ────────────────────────────────────────────────────

@dataclass
class NodeStats:
    src:          int
    last_temp_c:  float  = 0.0
    last_hum_pct: float  = 0.0
    last_rssi:    int    = 0
    last_snr:     int    = 0
    last_hops:    int    = 0
    last_uptime_s: int   = 0
    packet_count: int    = 0
    nack_count:   int    = 0
    first_seen:   float  = field(default_factory=time.time)
    last_seen:    float  = field(default_factory=time.time)

    rssi_min: int = 0
    rssi_max: int = 0
    rssi_sum: int = 0

    @property
    def rssi_avg(self) -> float:
        return self.rssi_sum / max(self.packet_count, 1)

    @property
    def age_s(self) -> float:
        return time.time() - self.last_seen


# ── Demo synthetic source ──────────────────────────────────────────────────

class DemoSource:
    """Generates synthetic frames for UI testing without hardware."""

    def __init__(self) -> None:
        self._q: queue.Queue[Frame] = queue.Queue(maxsize=64)
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self.line_count  = 0
        self.error_count = 0

    def start(self) -> None:
        self._stop.clear()
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()

    def get(self, timeout: float = 0.1) -> Frame | None:
        try:
            return self._q.get(timeout=timeout)
        except queue.Empty:
            return None

    def _run(self) -> None:
        import math, random
        t0   = time.time()
        seq  = 0
        nodes = [2, 3, 4]

        while not self._stop.is_set():
            elapsed = time.time() - t0
            # Rotate which node sends
            src = nodes[seq % len(nodes)]
            hops = (src - 1)  # node 2 = 1 hop, node 3 = 2 hops, etc.
            frame = DataRecord(
                src      = src,
                rssi     = -60 - hops * 15 + random.randint(-5, 5),
                snr      = 9 - hops * 2 + random.randint(-1, 1),
                hops     = hops,
                temp_c   = 22.0 + 3 * math.sin(elapsed / 120) + random.gauss(0, 0.2),
                hum_pct  = 55.0 + 5 * math.cos(elapsed / 180) + random.gauss(0, 0.5),
                uptime_s = int(elapsed) + src * 10,
            )
            try:
                self._q.put_nowait(frame)
            except queue.Full:
                pass

            # Status frame every ~10 frames
            if seq % 10 == 0:
                status = StatusRecord(
                    addr=1, routes=len(nodes),
                    tx=seq, rx=seq * 3, relay=seq,
                    drop_dup=seq // 20, drop_ttl=0, drop_no_route=0,
                )
                try:
                    self._q.put_nowait(status)
                except queue.Full:
                    pass

            seq += 1
            self.line_count += 1
            time.sleep(3.0)


# ── Dashboard renderer ─────────────────────────────────────────────────────

class Dashboard:
    def __init__(self, source, log_path: str | None = None) -> None:
        self._src    = source
        self._nodes: dict[int, NodeStats] = {}
        self._status: StatusRecord | None = None
        self._log    = open(log_path, "w") if log_path else None
        self._running = True
        self._start   = time.time()

        if self._log:
            self._log.write("time_iso,src,rssi,snr,hops,temp_c,hum_pct,uptime_s\n")

    def run(self, stdscr: "curses._CursesWindow") -> None:
        curses.curs_set(0)
        stdscr.nodelay(True)
        curses.start_color()
        curses.use_default_colors()
        curses.init_pair(1, curses.COLOR_GREEN,  -1)
        curses.init_pair(2, curses.COLOR_YELLOW, -1)
        curses.init_pair(3, curses.COLOR_RED,    -1)
        curses.init_pair(4, curses.COLOR_CYAN,   -1)
        curses.init_pair(5, curses.COLOR_WHITE,  -1)

        while self._running:
            # Process incoming frames
            frame = self._src.get(timeout=0)
            if frame is not None:
                self._handle(frame)

            # Keyboard
            ch = stdscr.getch()
            if ch in (ord('q'), ord('Q')):
                break
            if ch in (ord('r'), ord('R')):
                self._nodes.clear()

            self._draw(stdscr)
            time.sleep(0.1)

        if self._log:
            self._log.close()

    def _handle(self, frame: Frame) -> None:
        if isinstance(frame, DataRecord):
            ns = self._nodes.setdefault(frame.src, NodeStats(src=frame.src,
                                                               first_seen=frame.rx_time))
            ns.last_temp_c   = frame.temp_c
            ns.last_hum_pct  = frame.hum_pct
            ns.last_rssi     = frame.rssi
            ns.last_snr      = frame.snr
            ns.last_hops     = frame.hops
            ns.last_uptime_s = frame.uptime_s
            ns.last_seen     = frame.rx_time
            ns.packet_count += 1
            ns.rssi_sum     += frame.rssi
            if ns.packet_count == 1:
                ns.rssi_min = ns.rssi_max = frame.rssi
            else:
                ns.rssi_min = min(ns.rssi_min, frame.rssi)
                ns.rssi_max = max(ns.rssi_max, frame.rssi)

            if self._log:
                ts = datetime.fromtimestamp(frame.rx_time).isoformat(timespec="seconds")
                self._log.write(
                    f"{ts},{frame.src},{frame.rssi},{frame.snr},{frame.hops},"
                    f"{frame.temp_c:.2f},{frame.hum_pct:.2f},{frame.uptime_s}\n"
                )
                self._log.flush()

        elif isinstance(frame, StatusRecord):
            self._status = frame

        elif isinstance(frame, NackRecord):
            ns = self._nodes.get(frame.dest)
            if ns:
                ns.nack_count += 1

    def _rssi_color(self, rssi: int) -> int:
        if rssi >= -80: return curses.color_pair(1)   # green
        if rssi >= -100: return curses.color_pair(2)  # yellow
        return curses.color_pair(3)                   # red

    def _draw(self, stdscr: "curses._CursesWindow") -> None:
        stdscr.erase()
        h, w = stdscr.getmaxyx()
        now  = time.time()

        # Title bar
        title = f" LoRa Mesh Dashboard  |  uptime {int(now - self._start)}s  |  [q]uit [r]eset "
        stdscr.attron(curses.color_pair(4) | curses.A_BOLD)
        stdscr.addstr(0, 0, title[:w].ljust(w))
        stdscr.attroff(curses.color_pair(4) | curses.A_BOLD)

        # Column headers
        hdr = (f"{'Node':>5}  {'Temp°C':>7}  {'Hum%':>6}  "
               f"{'RSSI':>5}  {'SNR':>4}  {'Hops':>4}  "
               f"{'Pkts':>5}  {'NACKs':>5}  {'Age':>6}  {'Avg RSSI':>8}")
        stdscr.attron(curses.A_UNDERLINE)
        stdscr.addstr(2, 0, hdr[:w])
        stdscr.attroff(curses.A_UNDERLINE)

        row = 3
        for src in sorted(self._nodes):
            if row >= h - 4:
                break
            ns = self._nodes[src]
            age = ns.age_s
            age_str = f"{age:.0f}s" if age < 3600 else f"{age/3600:.1f}h"
            line = (f"{src:>5}  {ns.last_temp_c:>7.2f}  {ns.last_hum_pct:>6.1f}  "
                    f"{ns.last_rssi:>5}  {ns.last_snr:>4}  {ns.last_hops:>4}  "
                    f"{ns.packet_count:>5}  {ns.nack_count:>5}  {age_str:>6}  "
                    f"{ns.rssi_avg:>8.1f}")
            color = self._rssi_color(ns.last_rssi)
            if age > 60:
                color = curses.color_pair(3)
            stdscr.addstr(row, 0, line[:w], color)
            row += 1

        # Status bar
        if self._status is not None:
            s = self._status
            sline = (f" Base: routes={s.routes} tx={s.tx} rx={s.rx} "
                     f"relay={s.relay} dup={s.drop_dup} "
                     f"ttl={s.drop_ttl} noroute={s.drop_no_route}")
            if h > row + 2:
                stdscr.attron(curses.color_pair(4))
                stdscr.addstr(h - 2, 0, sline[:w].ljust(w))
                stdscr.attroff(curses.color_pair(4))

        lines_str = f" lines={self._src.line_count} err={self._src.error_count}"
        stdscr.addstr(h - 1, 0, lines_str[:w])

        stdscr.refresh()


# ── Entry point ────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(description="LoRa Mesh Dashboard")
    parser.add_argument("--port",  default=None)
    parser.add_argument("--baud",  type=int, default=115200)
    parser.add_argument("--demo",  action="store_true")
    parser.add_argument("--log",   default=None, help="CSV log output path")
    args = parser.parse_args()

    if args.demo:
        source = DemoSource()
    elif args.port:
        from protocol import SerialReader
        source = SerialReader(args.port, args.baud)
    else:
        parser.error("Specify --port <device> or --demo")

    source.start()

    dashboard = Dashboard(source, log_path=args.log)
    try:
        curses.wrapper(dashboard.run)
    finally:
        source.stop()


if __name__ == "__main__":
    main()
