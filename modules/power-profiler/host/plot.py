"""
plot.py — live current timeline with annotation overlays and section statistics.

Three panels:
  1. Current vs. time (scrolling, live)
     Annotation channel spans shown as coloured background bands.
  2. Per-section statistics table (mean, peak, RMS, energy) — updated after
     each closed annotation section.
  3. Energy bar chart — one bar per closed section, grouped by channel.

Usage
-----
    python main.py --port /dev/ttyACM0 [--duration 10] [--supply 3300]
"""

from __future__ import annotations

import time
from typing import Optional

import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
import matplotlib.patches as mpatches
import matplotlib.ticker as ticker
import numpy as np
from matplotlib.animation import FuncAnimation
from matplotlib.table import Table

from capture import CaptureSession, SectionStats

# ── Colour palette ─────────────────────────────────────────────────────────────
TRACE_COLOR  = "#0077BB"
ANN_COLORS   = ["#EE7733", "#009988", "#AA3377", "#EE3377"]
ANN_LABELS   = ["Ann 0", "Ann 1", "Ann 2", "Ann 3"]
C_GRID       = "#DDDDDD"

POLL_MS      = 200    # animation interval
WINDOW_S     = 10.0   # seconds of history shown in live plot


class Dashboard:
    def __init__(self, session: CaptureSession,
                 window_s: float = WINDOW_S,
                 supply_mV: float = 3300.0) -> None:
        self._session   = session
        self._window_s  = window_s
        self._supply_mV = supply_mV
        self._anim: Optional[FuncAnimation] = None
        self._build()

    def _build(self) -> None:
        self._fig = plt.figure(figsize=(14, 9))
        self._fig.canvas.manager.set_window_title("Power Profiler")

        gs = gridspec.GridSpec(
            3, 2,
            figure=self._fig,
            height_ratios=[3, 1, 1],
            hspace=0.50, wspace=0.35,
            left=0.08, right=0.97,
            top=0.93,  bottom=0.06,
        )

        # ── Panel 1: live current trace ───────────────────────────────────────
        self._ax_trace = self._fig.add_subplot(gs[0, :])
        self._ax_trace.set_title("Current vs. Time", fontsize=10)
        self._ax_trace.set_xlabel("Time (s)")
        self._ax_trace.set_ylabel("Current (mA)")
        self._ax_trace.set_ylim(-5, 500)
        self._ax_trace.grid(True, color=C_GRID, lw=0.5)
        self._line_trace, = self._ax_trace.plot([], [], color=TRACE_COLOR, lw=0.8)

        # Legend patches for annotation channels
        patches = [mpatches.Patch(color=ANN_COLORS[i], alpha=0.25, label=ANN_LABELS[i])
                   for i in range(4)]
        self._ax_trace.legend(handles=patches, loc="upper right", fontsize=7)

        # ── Panel 2: statistics table ─────────────────────────────────────────
        self._ax_table = self._fig.add_subplot(gs[1, :])
        self._ax_table.axis("off")
        self._ax_table.set_title("Section Statistics", fontsize=10)
        self._table_obj: Optional[Table] = None

        # ── Panel 3: energy bar chart ─────────────────────────────────────────
        self._ax_energy = self._fig.add_subplot(gs[2, :])
        self._ax_energy.set_title("Energy per Section (µJ)", fontsize=10)
        self._ax_energy.set_xlabel("Section")
        self._ax_energy.set_ylabel("Energy (µJ)")
        self._ax_energy.grid(True, axis="y", color=C_GRID, lw=0.5)

    def _draw_annotations(self, ax: plt.Axes, t: np.ndarray, ann: np.ndarray,
                           y_min: float, y_max: float) -> None:
        """Draw coloured spans for each active annotation channel."""
        for ch in range(4):
            bit   = 1 << ch
            color = ANN_COLORS[ch]
            active = (ann & bit) != 0

            start: Optional[float] = None
            for i, (ti, ai) in enumerate(zip(t, active)):
                if ai and start is None:
                    start = ti
                elif not ai and start is not None:
                    ax.axvspan(start, ti, alpha=0.18, color=color, lw=0)
                    start = None
            if start is not None:
                ax.axvspan(start, float(t[-1]), alpha=0.18, color=color, lw=0)

    def _update_table(self, sections: list[SectionStats]) -> None:
        if self._table_obj:
            self._table_obj.remove()
            self._table_obj = None

        if not sections:
            return

        cols  = ["Ch", "Duration (ms)", "Mean (mA)", "Peak (mA)", "RMS (mA)", "Energy (µJ)"]
        rows  = []
        colors = []
        for sec in sections[-12:]:     # show last 12 sections max
            rows.append([
                str(sec.channel),
                f"{sec.duration_s * 1000:.1f}",
                f"{sec.mean_mA:.2f}",
                f"{sec.peak_mA:.2f}",
                f"{sec.rms_mA:.2f}",
                f"{sec.energy_uJ:.1f}",
            ])
            colors.append([ANN_COLORS[sec.channel]] + ["white"] * 5)

        tbl = self._ax_table.table(
            cellText=rows,
            colLabels=cols,
            cellLoc="center",
            loc="center",
        )
        tbl.auto_set_font_size(False)
        tbl.set_fontsize(7)
        tbl.scale(1, 1.1)
        for (row, col), cell in tbl.get_celld().items():
            if row == 0:
                cell.set_facecolor("#CCCCCC")
            elif col == 0 and row > 0:
                cell.set_facecolor(ANN_COLORS[int(rows[row - 1][0])])
                cell.set_alpha(0.35)
        self._table_obj = tbl

    def _update_energy(self, sections: list[SectionStats]) -> None:
        self._ax_energy.cla()
        self._ax_energy.set_title("Energy per Section (µJ)", fontsize=10)
        self._ax_energy.set_ylabel("Energy (µJ)")
        self._ax_energy.grid(True, axis="y", color=C_GRID, lw=0.5)

        if not sections:
            return

        labels  = [f"Ch{s.channel}#{i}" for i, s in enumerate(sections)]
        values  = [s.energy_uJ for s in sections]
        colors  = [ANN_COLORS[s.channel] for s in sections]
        x       = np.arange(len(labels))

        self._ax_energy.bar(x, values, color=colors, alpha=0.75, width=0.6)
        self._ax_energy.set_xticks(x)
        self._ax_energy.set_xticklabels(labels, rotation=45, ha="right", fontsize=7)

    def _animate(self, _frame) -> None:
        t, c, ann = self._session.arrays()
        sections  = self._session.sections()

        if len(t) == 0:
            return

        # ── Live trace ────────────────────────────────────────────────────────
        now     = float(t[-1])
        x_min   = max(0.0, now - self._window_s)
        mask    = t >= x_min

        self._ax_trace.cla()
        self._ax_trace.set_title("Current vs. Time", fontsize=10)
        self._ax_trace.set_xlabel("Time (s)")
        self._ax_trace.set_ylabel("Current (mA)")
        self._ax_trace.grid(True, color=C_GRID, lw=0.5)
        self._ax_trace.set_xlim(x_min, now + 0.2)

        t_w, c_w, ann_w = t[mask], c[mask], ann[mask]
        if len(t_w):
            y_max = max(float(c_w.max()) * 1.15, 10.0)
            self._ax_trace.set_ylim(-2, y_max)
            self._ax_trace.plot(t_w, c_w, color=TRACE_COLOR, lw=0.8)
            self._draw_annotations(self._ax_trace, t_w, ann_w, -2, y_max)

        patches = [mpatches.Patch(color=ANN_COLORS[i], alpha=0.35, label=ANN_LABELS[i])
                   for i in range(4)]
        self._ax_trace.legend(handles=patches, loc="upper right", fontsize=7)

        # ── Stats table ───────────────────────────────────────────────────────
        self._ax_table.cla()
        self._ax_table.axis("off")
        self._ax_table.set_title("Section Statistics", fontsize=10)
        self._update_table(sections)

        # ── Energy chart ──────────────────────────────────────────────────────
        self._update_energy(sections)

    def start_animation(self) -> None:
        self._anim = FuncAnimation(
            self._fig,
            self._animate,
            interval=POLL_MS,
            blit=False,
            cache_frame_data=False,
        )

    def show(self) -> None:
        self.start_animation()
        plt.show()
