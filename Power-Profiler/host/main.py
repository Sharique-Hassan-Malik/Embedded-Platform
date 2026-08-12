"""
main.py — power profiler host application.

Connects to the Arduino firmware, streams samples into a CaptureSession,
displays a live Matplotlib dashboard and optionally saves a CSV on exit.

Usage
-----
    python main.py --port /dev/ttyACM0
    python main.py --port COM3 --duration 30 --supply 3300 --out capture.csv

Arguments
---------
--port      Serial port connected to the profiler Arduino
--baud      Serial baud rate (default 1000000)
--duration  Capture duration in seconds (0 = run until Ctrl+C)
--supply    DUT supply voltage in mV for energy calculation (default 3300)
--window    Seconds of history shown in the live plot (default 10)
--out       Optional CSV output path
"""

from __future__ import annotations

import argparse
import csv
import signal
import sys
import time

from transport import Profiler
from capture import CaptureSession
from plot import Dashboard


def save_csv(session: CaptureSession, path: str) -> None:
    t, c, ann = session.arrays()
    sections  = session.sections()

    with open(path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["time_s", "current_mA", "ann_mask"])
        for ti, ci, ai in zip(t, c, ann):
            writer.writerow([f"{ti:.6f}", f"{ci:.2f}", int(ai)])

    # Append section summary as a second block separated by a blank line.
    with open(path, "a", newline="") as f:
        writer = csv.writer(f)
        writer.writerow([])
        writer.writerow(["channel", "start_s", "end_s", "duration_ms",
                         "mean_mA", "peak_mA", "rms_mA", "energy_uJ"])
        for sec in sections:
            writer.writerow([
                sec.channel,
                f"{sec.start_s:.6f}",
                f"{sec.end_s:.6f}" if sec.end_s else "",
                f"{sec.duration_s * 1000:.2f}",
                f"{sec.mean_mA:.3f}",
                f"{sec.peak_mA:.3f}",
                f"{sec.rms_mA:.3f}",
                f"{sec.energy_uJ:.2f}",
            ])
    print(f"Saved {len(t)} samples and {len(sections)} sections to {path}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Power profiler host")
    parser.add_argument("--port",     required=True)
    parser.add_argument("--baud",     type=int,   default=1_000_000)
    parser.add_argument("--duration", type=float, default=0,
                        help="Capture duration in seconds (0 = until Ctrl+C)")
    parser.add_argument("--supply",   type=float, default=3300.0,
                        help="DUT supply voltage in mV (default 3300)")
    parser.add_argument("--window",   type=float, default=10.0,
                        help="Live plot history window in seconds (default 10)")
    parser.add_argument("--out",      default="",
                        help="Optional CSV output file path")
    args = parser.parse_args()

    session = CaptureSession(supply_mV=args.supply)

    with Profiler(args.port, args.baud) as prof:
        # Identify firmware and negotiate sample rate.
        try:
            rate = prof.identify()
            print(f"Firmware sample rate: {rate} S/s")
        except TimeoutError as e:
            print(f"Error: {e}", file=sys.stderr)
            sys.exit(1)

        prof.on_sample(session.push)
        prof.on_overflow(lambda: print("[!] Overflow — samples dropped",
                                       file=sys.stderr))

        prof.start()
        print("Capture started. Close the plot window or press Ctrl+C to stop.")

        dash = Dashboard(session, window_s=args.window, supply_mV=args.supply)

        # Stop after --duration seconds if specified.
        if args.duration > 0:
            import threading
            def _auto_stop():
                time.sleep(args.duration)
                prof.stop()
                print(f"\nCapture stopped after {args.duration} s.")
            threading.Thread(target=_auto_stop, daemon=True).start()

        try:
            dash.show()   # blocks until plot window is closed
        except KeyboardInterrupt:
            pass
        finally:
            prof.stop()

    if args.out:
        save_csv(session, args.out)
    else:
        t, _, _ = session.arrays()
        secs    = session.sections()
        print(f"\nCaptured {len(t)} samples, {len(secs)} annotated sections.")
        if secs:
            print("\nSection summary:")
            print(f"  {'Ch':>3}  {'Dur (ms)':>10}  {'Mean (mA)':>10}"
                  f"  {'Peak (mA)':>10}  {'RMS (mA)':>9}  {'Energy (µJ)':>12}")
            for sec in secs:
                print(f"  {sec.channel:>3}  {sec.duration_s*1000:>10.1f}"
                      f"  {sec.mean_mA:>10.2f}  {sec.peak_mA:>10.2f}"
                      f"  {sec.rms_mA:>9.2f}  {sec.energy_uJ:>12.1f}")


if __name__ == "__main__":
    main()
