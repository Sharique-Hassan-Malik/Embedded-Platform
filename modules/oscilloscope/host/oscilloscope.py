"""Digital Oscilloscope — Python host GUI (extended).

New in this version
-------------------
  FFT panel    — live single-sided power spectrum with Hann window, dBmV Y-axis,
                 up to 5 annotated peaks, switchable via a tab control.
  Cursors      — two draggable vertical cursors on the waveform (X1, X2) that
                 report ΔT and ΔV between them in the measurements panel.
  CSV capture  — Record / Stop button writes every incoming frame to a timestamped
                 CSV file; the status bar shows frame count and file size live.

Usage
-----
    python oscilloscope.py --port /dev/ttyACM0
    python oscilloscope.py --demo
    python oscilloscope.py --demo --out captures/demo.csv
"""

from __future__ import annotations

import argparse
import math
import os
import queue
import threading
import time
import tkinter as tk
from tkinter import ttk
from datetime import datetime

import numpy as np

import matplotlib
matplotlib.use("TkAgg")
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure

from protocol import (
    ADC_VREF_MV, DEFAULT_N_SAMPLES, DEFAULT_CLKDIV,
    clkdiv_to_sps, sps_to_clkdiv,
    cmd_start, cmd_stop, cmd_set_rate, cmd_set_channel, cmd_set_samples,
)
from signal_proc import (
    find_trigger, all_measurements,
    timebase_string, voltage_string,
    EDGE_RISING, EDGE_FALLING,
)
from fft_engine import compute_spectrum, find_peaks
from capture import Capture

# ── Constants ──────────────────────────────────────────────────────────────────
UPDATE_MS  = 33
N_DIVS_H   = 10
SAMPLE_RATES = [
    ("500 ksps", 500_000),
    ("200 ksps", 200_000),
    ("100 ksps", 100_000),
    (" 50 ksps",  50_000),
    (" 20 ksps",  20_000),
    (" 10 ksps",  10_000),
    ("  5 ksps",   5_000),
]
SAMPLE_COUNTS = [256, 512, 1024, 2048]
CHANNELS = ["ADC0 (GPIO26)", "ADC1 (GPIO27)", "ADC2 (GPIO28)", "Temp"]

# Catppuccin Mocha palette
C_BASE   = "#1e1e2e"
C_MANTLE = "#181825"
C_CRUST  = "#11111b"
C_SURFACE= "#313244"
C_OVERLAY= "#45475a"
C_MUTED  = "#6c7086"
C_TEXT   = "#cdd6f4"
C_BLUE   = "#89b4fa"
C_GREEN  = "#a6e3a1"
C_RED    = "#f38ba8"
C_YELLOW = "#f9e2af"
C_TEAL   = "#89dceb"
C_MAUVE  = "#cba6f7"
C_PEACH  = "#fab387"


# ── Demo signal generator ──────────────────────────────────────────────────────

class DemoReader:
    def __init__(self) -> None:
        self._q: queue.Queue = queue.Queue(maxsize=8)
        self._stop = threading.Event()
        self._sps  = clkdiv_to_sps(DEFAULT_CLKDIV)
        self._n    = DEFAULT_N_SAMPLES
        self._ch   = 0
        self._t    = 0.0
        self.drop_count  = 0
        self.frame_count = 0
        self._thread = threading.Thread(target=self._run, daemon=True)

    def start(self)  -> None: self._stop.clear(); self._thread.start()
    def stop(self)   -> None: self._stop.set()
    def send(self, _) -> None: pass
    def ping(self, timeout: float = 0.5) -> bool: return True

    def get_frame(self, timeout: float = 0.1):
        try:    return self._q.get(timeout=timeout)
        except queue.Empty: return None

    def _run(self) -> None:
        from dataclasses import dataclass

        @dataclass
        class Frame:
            channel: int; overflow: bool
            samples: np.ndarray; raw: np.ndarray; seq: int

        while not self._stop.is_set():
            period = self._n / self._sps
            t = np.linspace(self._t, self._t + period, self._n, endpoint=False)
            sig = (
                1200.0 * np.sin(2 * math.pi * 1000 * t)
                +  300.0 * np.sin(2 * math.pi * 3000 * t)
                +  150.0 * np.sin(2 * math.pi * 5000 * t)
                + 1650.0
                + np.random.normal(0, 12, self._n)
            )
            sig = np.clip(sig, 0, ADC_VREF_MV).astype(np.float32)
            raw = (sig * 4095 / ADC_VREF_MV).astype(np.uint16)
            frame = Frame(self._ch, False, sig, raw, self.frame_count)
            self.frame_count += 1
            self._t += period
            try:    self._q.put_nowait(frame)
            except queue.Full:
                self._q.get_nowait(); self._q.put_nowait(frame)
            time.sleep(period * 0.9)


# ── Cursor state ───────────────────────────────────────────────────────────────

class Cursor:
    """One draggable vertical cursor on the waveform axes."""

    def __init__(self, ax, x: float, color: str, label: str) -> None:
        self._ax    = ax
        self._x     = x
        self._color = color
        self._label = label
        self._line  = ax.axvline(x, color=color, linewidth=1.2,
                                  linestyle="--", alpha=0.85)
        self._text  = ax.text(x, ADC_VREF_MV * 0.95, label,
                               color=color, fontsize=8,
                               ha="center", va="top")
        self.visible = True

    @property
    def x(self) -> float:
        return self._x

    def set_x(self, x: float) -> None:
        self._x = x
        self._line.set_xdata([x, x])
        self._text.set_x(x)

    def set_visible(self, v: bool) -> None:
        self.visible = v
        self._line.set_visible(v)
        self._text.set_visible(v)

    def contains(self, event) -> bool:
        """True if the mouse event is within 8 pixels of this cursor line."""
        if not event.xdata:
            return False
        ax = self._ax
        # Convert data-x to display-x to compare pixel distance
        disp_cursor = ax.transData.transform((self._x, 0))[0]
        disp_event  = ax.transData.transform((event.xdata, 0))[0]
        return abs(disp_cursor - disp_event) < 8


# ── Main application ───────────────────────────────────────────────────────────

class OscilloscopeApp(tk.Tk):

    def __init__(self, reader, demo: bool = False,
                 out_path: str | None = None) -> None:
        super().__init__()
        self.title("Digital Oscilloscope — Raspberry Pi Pico")
        self.resizable(True, True)
        self.configure(bg=C_BASE)

        self._reader     = reader
        self._demo       = demo
        self._streaming  = False
        self._sps        = clkdiv_to_sps(DEFAULT_CLKDIV)
        self._n_samples  = DEFAULT_N_SAMPLES
        self._trig_level = ADC_VREF_MV / 2.0
        self._trig_edge  = EDGE_RISING
        self._last_frame = None
        self._last_window: np.ndarray | None = None

        # Cursor state
        self._cursors_enabled = False
        self._drag_cursor: Cursor | None = None
        self._cursor_x: Cursor | None    = None
        self._cursor_y: Cursor | None    = None

        # Capture
        default_out = out_path or os.path.join(
            "captures",
            f"capture_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv",
        )
        self._capture = Capture(default_out, self._sps)
        self._capture_path = default_out

        self._build_ui()
        self._after_id = self.after(UPDATE_MS, self._update)

    # ── UI construction ────────────────────────────────────────────────────────

    def _build_ui(self) -> None:
        toolbar = tk.Frame(self, bg=C_SURFACE, pady=4)
        toolbar.pack(side=tk.TOP, fill=tk.X)
        self._build_toolbar(toolbar)

        main = tk.Frame(self, bg=C_BASE)
        main.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        # Left: notebook with Waveform and FFT tabs
        nb_frame = tk.Frame(main, bg=C_BASE)
        nb_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        self._nb = ttk.Notebook(nb_frame)
        self._nb.pack(fill=tk.BOTH, expand=True, padx=4, pady=4)

        self._wave_tab = tk.Frame(self._nb, bg=C_BASE)
        self._fft_tab  = tk.Frame(self._nb, bg=C_BASE)
        self._nb.add(self._wave_tab, text="  Waveform  ")
        self._nb.add(self._fft_tab,  text="  Spectrum  ")
        self._nb.bind("<<NotebookTabChanged>>", self._on_tab_change)

        self._build_waveform(self._wave_tab)
        self._build_fft(self._fft_tab)

        self._build_measurements(main)

        self._status_var = tk.StringVar(value="Disconnected")
        tk.Label(self, textvariable=self._status_var,
                 bg=C_CRUST, fg=C_TEXT, anchor=tk.W, padx=6,
                 font=("Courier", 9)).pack(side=tk.BOTTOM, fill=tk.X)

    def _build_toolbar(self, parent: tk.Frame) -> None:
        lbl = {"bg": C_SURFACE, "fg": C_TEXT, "font": ("Helvetica", 10)}

        self._btn_start = tk.Button(parent, text="▶  Start", width=9,
                                    command=self._on_start,
                                    bg=C_GREEN, fg=C_MANTLE,
                                    font=("Helvetica", 10, "bold"), relief=tk.FLAT)
        self._btn_start.pack(side=tk.LEFT, padx=(8, 2), pady=2)

        self._btn_stop = tk.Button(parent, text="■  Stop", width=9,
                                   command=self._on_stop, state=tk.DISABLED,
                                   bg=C_RED, fg=C_MANTLE,
                                   font=("Helvetica", 10, "bold"), relief=tk.FLAT)
        self._btn_stop.pack(side=tk.LEFT, padx=(2, 8), pady=2)

        # Separator
        tk.Frame(parent, bg=C_OVERLAY, width=1).pack(side=tk.LEFT, fill=tk.Y, pady=2)

        tk.Label(parent, text=" Channel:", **lbl).pack(side=tk.LEFT)
        self._ch_var = tk.StringVar(value=CHANNELS[0])
        ch = ttk.Combobox(parent, textvariable=self._ch_var,
                          values=CHANNELS, width=16, state="readonly")
        ch.pack(side=tk.LEFT, padx=4)
        ch.bind("<<ComboboxSelected>>", self._on_channel)

        tk.Label(parent, text="  Rate:", **lbl).pack(side=tk.LEFT)
        self._rate_var = tk.StringVar(value=SAMPLE_RATES[3][0])
        rate = ttk.Combobox(parent, textvariable=self._rate_var,
                            values=[r[0] for r in SAMPLE_RATES],
                            width=10, state="readonly")
        rate.pack(side=tk.LEFT, padx=4)
        rate.bind("<<ComboboxSelected>>", self._on_rate)

        tk.Label(parent, text="  Samples:", **lbl).pack(side=tk.LEFT)
        self._nsamp_var = tk.StringVar(value=str(DEFAULT_N_SAMPLES))
        ns = ttk.Combobox(parent, textvariable=self._nsamp_var,
                          values=[str(n) for n in SAMPLE_COUNTS],
                          width=6, state="readonly")
        ns.pack(side=tk.LEFT, padx=4)
        ns.bind("<<ComboboxSelected>>", self._on_nsamples)

        tk.Frame(parent, bg=C_OVERLAY, width=1).pack(side=tk.LEFT, fill=tk.Y, pady=2)

        tk.Label(parent, text="  Trigger:", **lbl).pack(side=tk.LEFT)
        self._edge_var = tk.StringVar(value=EDGE_RISING)
        for edge in (EDGE_RISING, EDGE_FALLING):
            tk.Radiobutton(parent, text=edge.capitalize(),
                           variable=self._edge_var, value=edge,
                           bg=C_SURFACE, fg=C_TEXT, selectcolor=C_SURFACE,
                           activebackground=C_SURFACE,
                           command=self._on_edge).pack(side=tk.LEFT, padx=2)

        tk.Label(parent, text="  Level:", **lbl).pack(side=tk.LEFT)
        self._trig_var = tk.DoubleVar(value=self._trig_level)
        tk.Spinbox(parent, from_=0, to=ADC_VREF_MV, increment=50,
                   textvariable=self._trig_var, width=6,
                   bg=C_OVERLAY, fg=C_TEXT, insertbackground=C_TEXT,
                   command=self._on_trig_spin).pack(side=tk.LEFT, padx=4)

        tk.Frame(parent, bg=C_OVERLAY, width=1).pack(side=tk.LEFT, fill=tk.Y, pady=2)

        # Cursors toggle
        self._cursor_btn = tk.Button(parent, text="Cursors: Off", width=11,
                                     command=self._toggle_cursors,
                                     bg=C_OVERLAY, fg=C_TEXT,
                                     font=("Helvetica", 9), relief=tk.FLAT)
        self._cursor_btn.pack(side=tk.LEFT, padx=(4, 4), pady=2)

        tk.Frame(parent, bg=C_OVERLAY, width=1).pack(side=tk.LEFT, fill=tk.Y, pady=2)

        # CSV capture
        self._btn_rec = tk.Button(parent, text="⏺  Record", width=10,
                                  command=self._on_record,
                                  bg=C_MAUVE, fg=C_MANTLE,
                                  font=("Helvetica", 10, "bold"), relief=tk.FLAT)
        self._btn_rec.pack(side=tk.LEFT, padx=(4, 2), pady=2)

        self._btn_rec_stop = tk.Button(parent, text="⏹  Save", width=8,
                                       command=self._on_record_stop,
                                       state=tk.DISABLED,
                                       bg=C_OVERLAY, fg=C_TEXT,
                                       font=("Helvetica", 10, "bold"), relief=tk.FLAT)
        self._btn_rec_stop.pack(side=tk.LEFT, padx=(2, 8), pady=2)

    # ── Waveform panel ─────────────────────────────────────────────────────────

    def _build_waveform(self, parent: tk.Frame) -> None:
        fig = Figure(figsize=(9, 4), dpi=100, facecolor=C_BASE)
        self._wave_ax = fig.add_subplot(111, facecolor=C_MANTLE)
        ax = self._wave_ax

        ax.set_xlim(0, DEFAULT_N_SAMPLES)
        ax.set_ylim(-50, ADC_VREF_MV + 50)
        ax.set_xlabel("Sample", color=C_TEXT, fontsize=9)
        ax.set_ylabel("Voltage (mV)", color=C_TEXT, fontsize=9)
        ax.tick_params(colors=C_MUTED, labelsize=8)
        for spine in ax.spines.values():
            spine.set_edgecolor(C_OVERLAY)
        ax.grid(True, color=C_SURFACE, linewidth=0.5)

        self._wave_line, = ax.plot([], [], color=C_BLUE, linewidth=0.9)
        self._trig_line  = ax.axhline(self._trig_level, color=C_YELLOW,
                                       linewidth=1.0, linestyle="--", alpha=0.8)

        # Cursors (hidden until enabled)
        n = DEFAULT_N_SAMPLES
        self._cursor_x = Cursor(ax, n // 3,     C_GREEN, "X1")
        self._cursor_y = Cursor(ax, n * 2 // 3, C_PEACH, "X2")
        self._cursor_x.set_visible(False)
        self._cursor_y.set_visible(False)

        self._wave_canvas = FigureCanvasTkAgg(fig, master=parent)
        self._wave_canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)

        self._wave_canvas.mpl_connect("button_press_event",   self._mpl_press)
        self._wave_canvas.mpl_connect("motion_notify_event",  self._mpl_drag)
        self._wave_canvas.mpl_connect("button_release_event", self._mpl_release)

    # ── FFT panel ──────────────────────────────────────────────────────────────

    def _build_fft(self, parent: tk.Frame) -> None:
        fig = Figure(figsize=(9, 4), dpi=100, facecolor=C_BASE)
        self._fft_ax = fig.add_subplot(111, facecolor=C_MANTLE)
        ax = self._fft_ax

        ax.set_xlim(0, 1000)
        ax.set_ylim(-120, 20)
        ax.set_xlabel("Frequency (Hz)", color=C_TEXT, fontsize=9)
        ax.set_ylabel("Magnitude (dBmV)", color=C_TEXT, fontsize=9)
        ax.tick_params(colors=C_MUTED, labelsize=8)
        for spine in ax.spines.values():
            spine.set_edgecolor(C_OVERLAY)
        ax.grid(True, color=C_SURFACE, linewidth=0.5)

        self._fft_line, = ax.plot([], [], color=C_MAUVE, linewidth=0.9)
        self._fft_peaks_scatter = ax.scatter(
            [], [], color=C_PEACH, s=30, zorder=5,
        )
        # Peak annotation text objects (up to 5)
        self._fft_annots = [
            ax.annotate("", xy=(0, 0), xytext=(4, 6), textcoords="offset points",
                        color=C_PEACH, fontsize=7)
            for _ in range(5)
        ]

        self._fft_canvas = FigureCanvasTkAgg(fig, master=parent)
        self._fft_canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)

    # ── Measurements panel ─────────────────────────────────────────────────────

    def _build_measurements(self, parent: tk.Frame) -> None:
        mf = tk.Frame(parent, bg=C_MANTLE, width=200)
        mf.pack(side=tk.RIGHT, fill=tk.Y, padx=(0, 4), pady=4)
        mf.pack_propagate(False)

        def section(title: str) -> None:
            tk.Frame(mf, bg=C_SURFACE, height=1).pack(fill=tk.X, pady=(6, 2))
            tk.Label(mf, text=title, bg=C_MANTLE, fg=C_MUTED,
                     font=("Helvetica", 8, "bold")).pack(anchor=tk.W, padx=8)

        def row(label: str) -> tk.StringVar:
            f = tk.Frame(mf, bg=C_MANTLE)
            f.pack(fill=tk.X, padx=8, pady=1)
            tk.Label(f, text=label + ":", width=9, anchor=tk.W,
                     bg=C_MANTLE, fg=C_MUTED,
                     font=("Courier", 9)).pack(side=tk.LEFT)
            var = tk.StringVar(value="—")
            tk.Label(f, textvariable=var, anchor=tk.E, width=12,
                     bg=C_MANTLE, fg=C_TEXT,
                     font=("Courier", 9)).pack(side=tk.RIGHT)
            return var

        tk.Label(mf, text="Measurements", bg=C_MANTLE, fg=C_TEXT,
                 font=("Helvetica", 11, "bold")).pack(pady=(8, 2))

        section("Voltage")
        self._mv: dict[str, tk.StringVar] = {}
        for lbl in ("Vmin", "Vmax", "Vpp", "Vmean", "Vrms"):
            self._mv[lbl] = row(lbl)

        section("Signal")
        for lbl in ("Freq", "Duty%"):
            self._mv[lbl] = row(lbl)

        section("Cursors")
        for lbl in ("X1", "X2", "ΔT", "ΔV"):
            self._mv[lbl] = row(lbl)

        section("Capture")
        for lbl in ("Frames", "Drops", "Rec frames", "Rec size"):
            self._mv[lbl] = row(lbl)

        section("Timebase")
        self._tb_var = tk.StringVar(value="")
        tk.Label(mf, textvariable=self._tb_var, bg=C_MANTLE, fg=C_TEAL,
                 font=("Courier", 9), justify=tk.LEFT).pack(padx=8, anchor=tk.W)

    # ── Mouse events ───────────────────────────────────────────────────────────

    def _mpl_press(self, event) -> None:
        if event.inaxes != self._wave_ax:
            return
        # Trigger line drag (within 80 mV vertically)
        if abs(event.ydata - self._trig_level) < 80:
            self._drag_cursor = None
            self._drag_trig   = True
            return
        self._drag_trig = False
        # Cursor drag
        if self._cursors_enabled:
            for c in (self._cursor_x, self._cursor_y):
                if c.contains(event):
                    self._drag_cursor = c
                    return
        self._drag_cursor = None

    def _mpl_drag(self, event) -> None:
        if not event.inaxes:
            return
        if getattr(self, "_drag_trig", False):
            level = max(0.0, min(ADC_VREF_MV, event.ydata))
            self._trig_level = level
            self._trig_var.set(round(level, 1))
            self._trig_line.set_ydata([level, level])
            self._wave_canvas.draw_idle()
        elif self._drag_cursor is not None:
            x = max(0.0, event.xdata)
            self._drag_cursor.set_x(x)
            self._update_cursor_readout()
            self._wave_canvas.draw_idle()

    def _mpl_release(self, _event) -> None:
        self._drag_trig   = False
        self._drag_cursor = None

    # ── Cursor helpers ─────────────────────────────────────────────────────────

    def _toggle_cursors(self) -> None:
        self._cursors_enabled = not self._cursors_enabled
        self._cursor_btn.configure(
            text=f"Cursors: {'On' if self._cursors_enabled else 'Off'}",
            bg=C_TEAL if self._cursors_enabled else C_OVERLAY,
            fg=C_MANTLE if self._cursors_enabled else C_TEXT,
        )
        self._cursor_x.set_visible(self._cursors_enabled)
        self._cursor_y.set_visible(self._cursors_enabled)
        self._wave_canvas.draw_idle()
        if not self._cursors_enabled:
            for k in ("X1", "X2", "ΔT", "ΔV"):
                self._mv[k].set("—")

    def _update_cursor_readout(self) -> None:
        if not self._cursors_enabled or self._last_window is None:
            return

        x1 = self._cursor_x.x
        x2 = self._cursor_y.x
        step_us = 1_000_000.0 / self._sps
        dt_us   = abs(x2 - x1) * step_us

        def _mv_at(xi: float) -> float | None:
            idx = int(round(xi))
            if 0 <= idx < len(self._last_window):
                return float(self._last_window[idx])
            return None

        v1 = _mv_at(x1)
        v2 = _mv_at(x2)

        self._mv["X1"].set(f"{x1:.0f} smp")
        self._mv["X2"].set(f"{x2:.0f} smp")

        if dt_us < 1000:
            self._mv["ΔT"].set(f"{dt_us:.1f} µs")
        else:
            self._mv["ΔT"].set(f"{dt_us / 1000:.2f} ms")

        if v1 is not None and v2 is not None:
            self._mv["ΔV"].set(voltage_string(abs(v2 - v1)))
        else:
            self._mv["ΔV"].set("—")

    # ── Toolbar callbacks ──────────────────────────────────────────────────────

    def _on_start(self) -> None:
        self._reader.send(cmd_start())
        self._streaming = True
        self._btn_start.configure(state=tk.DISABLED)
        self._btn_stop.configure(state=tk.NORMAL)
        self._status_var.set("Streaming …")

    def _on_stop(self) -> None:
        self._reader.send(cmd_stop())
        self._streaming = False
        self._btn_start.configure(state=tk.NORMAL)
        self._btn_stop.configure(state=tk.DISABLED)
        self._status_var.set("Stopped")

    def _on_channel(self, _=None) -> None:
        idx = CHANNELS.index(self._ch_var.get())
        self._reader.send(cmd_set_channel(idx))

    def _on_rate(self, _=None) -> None:
        label = self._rate_var.get()
        sps   = next(r[1] for r in SAMPLE_RATES if r[0] == label)
        self._sps = sps
        self._capture._sps = sps
        self._reader.send(cmd_set_rate(sps_to_clkdiv(sps)))
        self._update_timebase_label()

    def _on_nsamples(self, _=None) -> None:
        n = int(self._nsamp_var.get())
        self._n_samples = n
        self._reader.send(cmd_set_samples(n))
        self._update_timebase_label()

    def _on_edge(self) -> None:
        self._trig_edge = self._edge_var.get()

    def _on_trig_spin(self) -> None:
        try:    self._trig_level = float(self._trig_var.get())
        except ValueError: pass
        self._trig_line.set_ydata([self._trig_level, self._trig_level])

    def _on_tab_change(self, _=None) -> None:
        pass   # future: pause FFT computation when waveform tab is hidden

    # ── CSV capture ────────────────────────────────────────────────────────────

    def _on_record(self) -> None:
        ch_idx = CHANNELS.index(self._ch_var.get())
        self._capture.open(channel=ch_idx)
        self._btn_rec.configure(state=tk.DISABLED)
        self._btn_rec_stop.configure(state=tk.NORMAL)
        self._status_var.set(f"Recording → {self._capture_path}")

    def _on_record_stop(self) -> None:
        self._capture.close()
        stats = self._capture.stats()
        self._btn_rec.configure(state=tk.NORMAL)
        self._btn_rec_stop.configure(state=tk.DISABLED)
        self._status_var.set(
            f"Saved {stats.frames} frames  ({stats.samples} samples, "
            f"{stats.size_bytes // 1024} KB) → {stats.path}"
        )

    # ── Update loop ────────────────────────────────────────────────────────────

    def _update(self) -> None:
        frame = self._reader.get_frame(timeout=0)
        if frame is not None:
            self._last_frame = frame
            if self._capture.is_open():
                self._capture.write(frame)

            tab_idx = self._nb.index(self._nb.select())
            if tab_idx == 0:
                self._render_waveform(frame)
            else:
                self._render_fft(frame)

        # Sidebar stats
        self._mv["Frames"].set(str(self._reader.frame_count))
        self._mv["Drops"].set(str(self._reader.drop_count))
        if self._capture.is_open():
            stats = self._capture.stats()
            self._mv["Rec frames"].set(str(stats.frames))
            self._mv["Rec size"].set(f"{stats.size_bytes // 1024} KB")

        self._after_id = self.after(UPDATE_MS, self._update)

    # ── Waveform rendering ─────────────────────────────────────────────────────

    def _render_waveform(self, frame) -> None:
        samples = frame.samples
        n = len(samples)
        if n == 0:
            return

        trig_idx = find_trigger(
            samples, self._trig_level, self._trig_edge,
            pre_samples=max(20, n // 8),
        )
        window = samples[trig_idx:]
        self._last_window = window

        x = np.arange(len(window))
        self._wave_line.set_data(x, window)
        self._wave_ax.set_xlim(0, max(len(window) - 1, 1))

        if self._cursors_enabled:
            self._update_cursor_readout()

        meas = all_measurements(window, self._sps, self._trig_level)
        for key, val in meas.items():
            lbl = key
            if lbl not in self._mv:
                continue
            if val is None:
                self._mv[lbl].set("—")
            elif key == "Freq":
                self._mv[lbl].set(
                    f"{val:.1f} Hz" if val < 1000 else f"{val / 1000:.2f} kHz"
                )
            elif key == "Duty%":
                self._mv[lbl].set(f"{val:.1f}%")
            else:
                self._mv[lbl].set(voltage_string(val))

        if frame.overflow:
            self._status_var.set("⚠ DMA overflow — reduce sample rate")

        self._wave_canvas.draw_idle()

    # ── FFT rendering ──────────────────────────────────────────────────────────

    def _render_fft(self, frame) -> None:
        samples = frame.samples
        if len(samples) < 8:
            return

        freq, mag = compute_spectrum(samples, self._sps, zero_pad=True)

        self._fft_line.set_data(freq, mag)
        self._fft_ax.set_xlim(0, self._sps / 2)
        self._fft_ax.set_ylim(max(-120, float(mag.min()) - 10), float(mag.max()) + 10)

        # Annotate top peaks
        sep = self._sps / 50   # minimum 2% of Nyquist separation
        peaks = find_peaks(freq, mag, n_peaks=5, min_separation_hz=sep,
                           threshold_dbmv=-80.0)

        if peaks:
            px = [p[0] for p in peaks]
            py = [p[1] for p in peaks]
            self._fft_peaks_scatter.set_offsets(np.column_stack([px, py]))
        else:
            self._fft_peaks_scatter.set_offsets(np.empty((0, 2)))

        for i, annot in enumerate(self._fft_annots):
            if i < len(peaks):
                f, m = peaks[i]
                annot.set_visible(True)
                annot.xy = (f, m)
                label = f"{f:.0f} Hz" if f < 1000 else f"{f / 1000:.2f} kHz"
                annot.set_text(label)
            else:
                annot.set_visible(False)

        self._fft_canvas.draw_idle()

    # ── Helpers ────────────────────────────────────────────────────────────────

    def _update_timebase_label(self) -> None:
        spd = self._n_samples / self._sps / N_DIVS_H
        self._tb_var.set(
            f"{timebase_string(spd)}\n"
            f"{self._sps / 1000:.0f} ksps  ·  {self._n_samples} smp\n"
            f"Nyquist: {self._sps / 2000:.1f} kHz"
        )

    def on_close(self) -> None:
        self.after_cancel(self._after_id)
        if self._capture.is_open():
            self._capture.close()
        self._reader.send(cmd_stop())
        self._reader.stop()
        self.destroy()


# ── Entry point ────────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(description="Digital Oscilloscope host")
    parser.add_argument("--port", default=None, help="Serial port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--demo", action="store_true",
                        help="Synthetic waveform — no hardware required")
    parser.add_argument("--out",  default=None,
                        help="CSV output path (default: captures/capture_<timestamp>.csv)")
    args = parser.parse_args()

    if args.demo:
        reader = DemoReader()
    elif args.port:
        from serial_reader import SerialReader
        reader = SerialReader(args.port, args.baud)
    else:
        parser.error("Specify --port <device> or --demo")

    reader.start()

    if not args.demo:
        print("Pinging firmware …", end=" ", flush=True)
        print("OK" if reader.ping() else "no response")

    app = OscilloscopeApp(reader, demo=args.demo, out_path=args.out)
    app.protocol("WM_DELETE_WINDOW", app.on_close)
    if args.demo:
        app.after(200, app._on_start)
    app.mainloop()


if __name__ == "__main__":
    main()
