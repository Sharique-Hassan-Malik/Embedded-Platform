"""
Spectrophotometer host application.

Provides three panels in a single Matplotlib window:
  1. Live absorbance readout — continuously polls the instrument and plots A vs. time.
  2. Calibration — enter known concentrations and measured absorbances,
     fit Beer-Lambert and display ε and R².
  3. Unknown — enter an absorbance (or press Read to capture one) and compute
     the unknown concentration from the calibration.

Usage
-----
    python main.py --port /dev/ttyACM0

Keyboard shortcuts (in the live plot):
    b   — take blank (store I0)
    d   — take dark reading
    q   — quit
"""

from __future__ import annotations

import argparse
import math
import sys
import time
from collections import deque
from typing import Optional

import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
import numpy as np
from matplotlib.animation import FuncAnimation
from matplotlib.widgets import Button, TextBox

from transport import Spectrophotometer, Reading
from beer_lambert import (
    CalibrationResult,
    FitResult,
    fit_beer_lambert,
    concentration_from_absorbance,
)

# ── Configuration ──────────────────────────────────────────────────────────────
HISTORY_LEN    = 120    # number of readings shown in the live plot
POLL_INTERVAL  = 500    # ms between live readings
PATH_LENGTH_CM = 1.0

# ── Colours (colorblind-safe palette) ─────────────────────────────────────────
C_SIGNAL  = "#0077BB"
C_FIT     = "#EE7733"
C_RESID   = "#AA3377"
C_CALIB   = "#009988"


class App:
    def __init__(self, port: str, baud: int = 115200) -> None:
        self._inst = Spectrophotometer(port, baud)
        self._calibration: Optional[CalibrationResult] = None

        # Live data buffers
        self._times: deque[float]      = deque(maxlen=HISTORY_LEN)
        self._abs_vals: deque[float]   = deque(maxlen=HISTORY_LEN)
        self._t0: float                = time.monotonic()

        # Calibration data entry storage
        self._cal_concs: list[float]  = []
        self._cal_abs:   list[float]  = []

        self._build_ui()

    # ── UI construction ───────────────────────────────────────────────────────

    def _build_ui(self) -> None:
        self._fig = plt.figure(figsize=(13, 8))
        self._fig.canvas.manager.set_window_title("Spectrophotometer")

        gs = gridspec.GridSpec(
            3, 2,
            figure=self._fig,
            hspace=0.55,
            wspace=0.35,
            left=0.08, right=0.97,
            top=0.93,  bottom=0.08,
        )

        # ── Panel 1: live absorbance ──────────────────────────────────────────
        self._ax_live = self._fig.add_subplot(gs[0, :])
        self._ax_live.set_title("Live Absorbance", fontsize=10)
        self._ax_live.set_xlabel("Time (s)")
        self._ax_live.set_ylabel("Absorbance (A)")
        self._line_live, = self._ax_live.plot([], [], color=C_SIGNAL, lw=1.5)
        self._ax_live.set_xlim(0, HISTORY_LEN * POLL_INTERVAL / 1000)
        self._ax_live.set_ylim(-0.05, 2.5)
        self._txt_live = self._ax_live.text(
            0.02, 0.88, "A = ---",
            transform=self._ax_live.transAxes,
            fontsize=13, fontweight="bold", color=C_SIGNAL,
        )

        # ── Panel 2: calibration curve ────────────────────────────────────────
        self._ax_cal = self._fig.add_subplot(gs[1, 0])
        self._ax_cal.set_title("Beer-Lambert Calibration", fontsize=10)
        self._ax_cal.set_xlabel("Concentration (mol/L)")
        self._ax_cal.set_ylabel("Absorbance (A)")
        self._scat_cal, = self._ax_cal.plot([], [], "o", color=C_CALIB, ms=6)
        self._line_cal, = self._ax_cal.plot([], [], "-",  color=C_FIT,   lw=1.5)
        self._txt_cal   = self._ax_cal.text(
            0.05, 0.88, "",
            transform=self._ax_cal.transAxes,
            fontsize=8,
        )

        # ── Panel 3: residuals ────────────────────────────────────────────────
        self._ax_res = self._fig.add_subplot(gs[1, 1])
        self._ax_res.set_title("Calibration Residuals", fontsize=10)
        self._ax_res.set_xlabel("Concentration (mol/L)")
        self._ax_res.set_ylabel("Residual (A)")
        self._ax_res.axhline(0, color="grey", lw=0.8, ls="--")
        self._scat_res, = self._ax_res.plot([], [], "o", color=C_RESID, ms=6)

        # ── Control widgets ───────────────────────────────────────────────────
        ax_blank = self._fig.add_axes([0.08, 0.02, 0.10, 0.04])
        ax_dark  = self._fig.add_axes([0.20, 0.02, 0.10, 0.04])
        ax_fit   = self._fig.add_axes([0.32, 0.02, 0.10, 0.04])
        ax_add   = self._fig.add_axes([0.44, 0.02, 0.10, 0.04])
        ax_unk   = self._fig.add_axes([0.60, 0.02, 0.18, 0.04])

        self._btn_blank = Button(ax_blank, "Blank (b)")
        self._btn_dark  = Button(ax_dark,  "Dark (d)")
        self._btn_fit   = Button(ax_fit,   "Fit curve")
        self._btn_add   = Button(ax_add,   "Add point")
        self._txt_unk   = TextBox(ax_unk, "Unknown A → c: ", initial="0.000")

        self._btn_blank.on_clicked(lambda _: self._do_blank())
        self._btn_dark.on_clicked(lambda _:  self._do_dark())
        self._btn_fit.on_clicked(lambda _:   self._do_fit())
        self._btn_add.on_clicked(lambda _:   self._add_cal_point())
        self._txt_unk.on_submit(self._compute_unknown)

        # Keyboard shortcuts
        self._fig.canvas.mpl_connect("key_press_event", self._on_key)

        # ── Animation ─────────────────────────────────────────────────────────
        self._anim = FuncAnimation(
            self._fig,
            self._poll,
            interval=POLL_INTERVAL,
            blit=False,
            cache_frame_data=False,
        )

    # ── Live polling ──────────────────────────────────────────────────────────

    def _poll(self, _frame) -> None:
        try:
            r: Reading = self._inst.read()
        except Exception as exc:
            print(f"Read error: {exc}", file=sys.stderr)
            return

        t = time.monotonic() - self._t0
        self._times.append(t)

        a = r.absorbance if not math.isnan(r.absorbance) else 0.0
        self._abs_vals.append(r.absorbance)

        ts = np.asarray(self._times)
        As = np.asarray(self._abs_vals, dtype=float)
        # Replace nan with 0 for display; nan gaps show as line breaks
        self._line_live.set_data(ts, As)

        # Slide x-axis to keep current time in view
        window = HISTORY_LEN * POLL_INTERVAL / 1000
        self._ax_live.set_xlim(max(0, t - window), t + 1)

        if not math.isnan(r.absorbance):
            self._txt_live.set_text(f"A = {r.absorbance:.4f}")
        else:
            self._txt_live.set_text("A = --- (no blank)")

    # ── Instrument actions ────────────────────────────────────────────────────

    def _do_blank(self) -> None:
        try:
            i0 = self._inst.blank()
            print(f"Blank stored: I0 = {i0} ADC counts")
        except Exception as exc:
            print(f"Blank error: {exc}", file=sys.stderr)

    def _do_dark(self) -> None:
        try:
            dark = self._inst.dark()
            print(f"Dark stored: {dark} ADC counts")
        except Exception as exc:
            print(f"Dark error: {exc}", file=sys.stderr)

    # ── Calibration ───────────────────────────────────────────────────────────

    def _add_cal_point(self) -> None:
        """Add the current live absorbance reading as a calibration point."""
        if not self._abs_vals:
            print("No live reading available yet.", file=sys.stderr)
            return
        A = self._abs_vals[-1]
        if math.isnan(A):
            print("Cannot add point: no blank stored.", file=sys.stderr)
            return

        conc_str = self._txt_unk.text.strip()
        try:
            c = float(conc_str)
        except ValueError:
            print(f"Invalid concentration: {conc_str!r}", file=sys.stderr)
            return

        self._cal_concs.append(c)
        self._cal_abs.append(A)
        print(f"Calibration point added: c={c} mol/L, A={A:.4f}")

    def _do_fit(self) -> None:
        if len(self._cal_concs) < 2:
            print("Need at least 2 calibration points.", file=sys.stderr)
            return

        result: FitResult = fit_beer_lambert(
            self._cal_concs,
            self._cal_abs,
            path_length_cm=PATH_LENGTH_CM,
        )
        self._calibration = result.calibration
        self._update_cal_plot(result)

    def _update_cal_plot(self, result: FitResult) -> None:
        c     = result.concentrations
        A     = result.absorbances
        cal   = result.calibration

        self._scat_cal.set_data(c, A)

        c_fit = np.linspace(0, c.max() * 1.1, 200)
        A_fit = cal.slope * c_fit + cal.intercept
        self._line_cal.set_data(c_fit, A_fit)

        self._ax_cal.relim()
        self._ax_cal.autoscale_view()

        self._txt_cal.set_text(
            f"ε = {cal.epsilon:.1f} L mol⁻¹ cm⁻¹\n"
            f"R² = {cal.r_squared:.5f}"
        )

        self._scat_res.set_data(c, result.residuals)
        self._ax_res.relim()
        self._ax_res.autoscale_view()

        self._fig.canvas.draw_idle()

    # ── Unknown concentration ─────────────────────────────────────────────────

    def _compute_unknown(self, text: str) -> None:
        if self._calibration is None:
            print("No calibration fitted yet.", file=sys.stderr)
            return
        try:
            A_unk = float(text.strip())
        except ValueError:
            print(f"Invalid absorbance: {text!r}", file=sys.stderr)
            return

        c_unk = concentration_from_absorbance(A_unk, self._calibration)
        if math.isnan(c_unk):
            print("Cannot compute concentration (check calibration).", file=sys.stderr)
        else:
            print(f"A = {A_unk:.4f}  →  c = {c_unk:.6f} mol/L")

    # ── Keyboard shortcuts ────────────────────────────────────────────────────

    def _on_key(self, event) -> None:
        if event.key == "b":
            self._do_blank()
        elif event.key == "d":
            self._do_dark()
        elif event.key == "q":
            plt.close("all")

    def run(self) -> None:
        plt.show()
        self._inst.close()


# ── Entry point ───────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(description="Spectrophotometer host")
    parser.add_argument("--port", required=True, help="Serial port (e.g. /dev/ttyACM0)")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    app = App(args.port, args.baud)
    app.run()


if __name__ == "__main__":
    main()
