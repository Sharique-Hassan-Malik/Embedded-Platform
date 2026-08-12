"""
Wireless sensor mesh dashboard.

Displays a live Matplotlib window with one row of panels per discovered node:
  Temperature (°C) — scrolling line plot
  Humidity (%)     — scrolling line plot
  Light level (%)  — scrolling line plot
  Motion           — event markers on a timeline

Usage
-----
    python dashboard.py --port /dev/ttyACM0

Optional arguments:
    --baud   115200       serial baud rate
    --window 120          seconds of history shown in scrolling plots
    --max-nodes 4         maximum rows (nodes) to display
"""

from __future__ import annotations

import argparse
import collections
import sys
import time
from typing import Deque, Dict

import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
from matplotlib.animation import FuncAnimation
import matplotlib.patches as mpatches

from transport import BaseTransport, NodeReading

# ── Colour palette (colorblind-safe) ─────────────────────────────────────────
NODE_COLOURS = ["#0077BB", "#EE7733", "#009988", "#AA3377",
                "#CC3311", "#33BBEE", "#EE3377", "#BBBBBB"]
C_MOTION     = "#EE3377"
C_GRID       = "#DDDDDD"

POLL_MS      = 500   # animation refresh interval


class NodeBuffer:
    """Stores recent readings for one node."""

    def __init__(self, window_s: float) -> None:
        maxlen = int(window_s * 2) + 10
        self.times: Deque[float]         = collections.deque(maxlen=maxlen)
        self.temperature: Deque[float]   = collections.deque(maxlen=maxlen)
        self.humidity: Deque[float]      = collections.deque(maxlen=maxlen)
        self.light: Deque[float]         = collections.deque(maxlen=maxlen)
        self.motion_times: Deque[float]  = collections.deque(maxlen=maxlen)
        self.hops: int = 0

    def push(self, r: NodeReading, t0: float) -> None:
        t = time.monotonic() - t0
        self.times.append(t)
        self.temperature.append(r.temperature if r.temperature is not None else float("nan"))
        self.humidity.append(r.humidity    if r.humidity    is not None else float("nan"))
        self.light.append(r.light_pct)
        if r.motion:
            self.motion_times.append(t)
        self.hops = r.hops


class Dashboard:
    def __init__(self, port: str, baud: int, window_s: float, max_nodes: int) -> None:
        self._transport = BaseTransport(port, baud)
        self._transport.on_reading(self._on_reading)
        self._transport.on_log(lambda msg: print(f"[base] {msg}", file=sys.stderr))

        self._window_s = window_s
        self._max_nodes = max_nodes
        self._t0: float = time.monotonic()

        self._buffers: Dict[int, NodeBuffer] = {}
        self._node_order: list[int] = []

        self._fig: plt.Figure | None = None
        self._axes: Dict[int, dict] = {}   # node_id → {temp, hum, light, motion axes}
        self._lines: Dict[int, dict] = {}  # node_id → {temp, hum, light line artists}
        self._built = False

    def _on_reading(self, r: NodeReading) -> None:
        if r.node_id not in self._buffers:
            if len(self._buffers) >= self._max_nodes:
                return
            self._buffers[r.node_id] = NodeBuffer(self._window_s)
            self._node_order.append(r.node_id)
            self._built = False  # trigger layout rebuild
        self._buffers[r.node_id].push(r, self._t0)

    def _build_layout(self) -> None:
        if self._fig:
            plt.close(self._fig)

        n = len(self._node_order)
        if n == 0:
            return

        self._fig = plt.figure(figsize=(14, 3.2 * n + 1.0))
        self._fig.canvas.manager.set_window_title("Sensor Mesh Dashboard")

        gs = gridspec.GridSpec(
            n, 4,
            figure=self._fig,
            hspace=0.55, wspace=0.38,
            left=0.07, right=0.97,
            top=0.93,   bottom=0.08,
        )

        self._axes = {}

        for row, node_id in enumerate(self._node_order):
            colour = NODE_COLOURS[node_id % len(NODE_COLOURS)]

            ax_t = self._fig.add_subplot(gs[row, 0])
            ax_h = self._fig.add_subplot(gs[row, 1])
            ax_l = self._fig.add_subplot(gs[row, 2])
            ax_m = self._fig.add_subplot(gs[row, 3])

            for ax, title, ylim, ylabel in [
                (ax_t, "Temperature (°C)", (0, 50),    "°C"),
                (ax_h, "Humidity (%)",     (0, 100),   "%"),
                (ax_l, "Light (%)",        (0, 100),   "%"),
            ]:
                ax.set_title(f"Node {node_id} — {title}", fontsize=9)
                ax.set_ylim(*ylim)
                ax.set_ylabel(ylabel, fontsize=8)
                ax.set_xlabel("t (s)", fontsize=7)
                ax.grid(True, color=C_GRID, lw=0.5)
                ax.tick_params(labelsize=7)

            ax_m.set_title(f"Node {node_id} — Motion", fontsize=9)
            ax_m.set_ylim(-0.1, 1.1)
            ax_m.set_yticks([0, 1])
            ax_m.set_yticklabels(["off", "on"], fontsize=7)
            ax_m.set_xlabel("t (s)", fontsize=7)
            ax_m.grid(True, color=C_GRID, lw=0.5)
            ax_m.tick_params(labelsize=7)

            l_t, = ax_t.plot([], [], color=colour, lw=1.5)
            l_h, = ax_h.plot([], [], color=colour, lw=1.5)
            l_l, = ax_l.plot([], [], color=colour, lw=1.5)

            self._axes[node_id] = dict(temp=ax_t, hum=ax_h, light=ax_l, motion=ax_m)
            self._lines[node_id] = dict(temp=l_t, hum=l_h, light=l_l)

        self._built = True

    def _animate(self, _frame) -> None:
        if not self._built or not self._node_order:
            if self._node_order:
                self._build_layout()
            return

        now = time.monotonic() - self._t0
        x_min = max(0.0, now - self._window_s)
        x_max = now + 2.0

        for node_id in self._node_order:
            buf  = self._buffers[node_id]
            axs  = self._axes[node_id]
            lns  = self._lines[node_id]

            ts = list(buf.times)

            lns["temp"].set_data(ts, list(buf.temperature))
            lns["hum"].set_data(ts, list(buf.humidity))
            lns["light"].set_data(ts, list(buf.light))

            for key in ("temp", "hum", "light"):
                axs[key].set_xlim(x_min, x_max)

            # Motion: redraw vertical lines for recent motion events.
            ax_m = axs["motion"]
            ax_m.cla()
            ax_m.set_title(
                f"Node {node_id} — Motion  [hops: {buf.hops}]", fontsize=9
            )
            ax_m.set_ylim(-0.1, 1.1)
            ax_m.set_yticks([0, 1])
            ax_m.set_yticklabels(["off", "on"], fontsize=7)
            ax_m.set_xlim(x_min, x_max)
            ax_m.set_xlabel("t (s)", fontsize=7)
            ax_m.grid(True, color=C_GRID, lw=0.5)
            ax_m.tick_params(labelsize=7)
            for mt in buf.motion_times:
                if mt >= x_min:
                    ax_m.axvline(mt, color=C_MOTION, lw=1.2, alpha=0.8)

    def run(self) -> None:
        # Start with a placeholder figure; _build_layout() runs once first node arrives.
        self._fig = plt.figure(figsize=(10, 2))
        plt.text(0.5, 0.5, "Waiting for sensor nodes…",
                 ha="center", va="center", fontsize=14, transform=plt.gca().transAxes)

        anim = FuncAnimation(
            self._fig,
            self._animate,
            interval=POLL_MS,
            blit=False,
            cache_frame_data=False,
        )
        plt.show()
        self._transport.close()


def main() -> None:
    parser = argparse.ArgumentParser(description="Sensor mesh dashboard")
    parser.add_argument("--port",      required=True)
    parser.add_argument("--baud",      type=int,   default=115200)
    parser.add_argument("--window",    type=float, default=120.0,
                        help="Seconds of history to display (default 120)")
    parser.add_argument("--max-nodes", type=int,   default=4,
                        help="Maximum number of nodes to show (default 4)")
    args = parser.parse_args()

    app = Dashboard(args.port, args.baud, args.window, args.max_nodes)
    app.run()


if __name__ == "__main__":
    main()
