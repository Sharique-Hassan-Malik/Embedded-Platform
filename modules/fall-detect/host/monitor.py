"""
Serial monitor for the fall detection device.

Connects to the Arduino over USB serial, displays live state and IMU data,
and optionally captures raw IMU samples to a CSV file for offline analysis.

Usage:
    python monitor.py --port /dev/ttyUSB0
    python monitor.py --port COM3 --baud 115200 --capture capture.csv
    python monitor.py --port /dev/ttyUSB0 --plot     # live SMV plot

The device sends lines in the following formats:
    State transitions : "State: IDLE → FREE_FALL"
    Classifier output : "Classifier: fall=0.921  no_fall=0.079"
    Alerts            : "*** FALL CONFIRMED ***"
    IMU data (optional, if firmware has DEBUG_IMU enabled):
                        "IMU: ax=0.012 ay=-0.034 az=0.997 gx=0.12 gy=-0.45 gz=0.03"
"""

from __future__ import annotations

import argparse
import csv
import re
import time
from datetime import datetime
from pathlib import Path

ANSI_RED    = "\033[91m"
ANSI_YELLOW = "\033[93m"
ANSI_GREEN  = "\033[92m"
ANSI_RESET  = "\033[0m"

STATE_COLORS = {
    "IDLE":        ANSI_GREEN,
    "FREE_FALL":   ANSI_YELLOW,
    "IMPACT":      ANSI_YELLOW,
    "CLASSIFYING": ANSI_YELLOW,
    "LYING":       ANSI_YELLOW,
    "ALERT":       ANSI_RED,
    "COOLDOWN":    ANSI_RESET,
}

IMU_RE    = re.compile(
    r"IMU:\s*ax=([-\d.]+)\s+ay=([-\d.]+)\s+az=([-\d.]+)"
    r"\s+gx=([-\d.]+)\s+gy=([-\d.]+)\s+gz=([-\d.]+)")
STATE_RE  = re.compile(r"State:\s*(\w+)\s*→\s*(\w+)")
CONF_RE   = re.compile(r"Classifier:\s*fall=([\d.]+)\s+no_fall=([\d.]+)")


def monitor(port: str, baud: int,
            capture_path: str | None,
            do_plot: bool) -> None:
    try:
        import serial
    except ImportError:
        print("pyserial not installed.  Run: pip install pyserial")
        return

    ser = serial.Serial(port, baud, timeout=0.1)
    print(f"Connected to {port} at {baud} baud")

    csv_file = csv_writer = None
    if capture_path:
        csv_file   = open(capture_path, "w", newline="")
        csv_writer = csv.writer(csv_file)
        csv_writer.writerow(["timestamp", "ax", "ay", "az",
                              "gx", "gy", "gz", "smv"])
        print(f"Capturing IMU data to {capture_path}")

    smv_buf: list[float] = []
    time_buf: list[float] = []
    t0 = time.monotonic()

    if do_plot:
        import matplotlib.pyplot as plt
        import matplotlib.animation as animation

        fig, ax = plt.subplots(figsize=(10, 4))
        ax.set_xlabel("Time (s)")
        ax.set_ylabel("SMV (g)")
        ax.set_title("Signal Magnitude Vector — Live")
        ax.axhline(y=0.5, color="orange", linestyle="--", alpha=0.7, label="Free-fall thr")
        ax.axhline(y=3.0, color="red",    linestyle="--", alpha=0.7, label="Impact thr")
        ax.legend(loc="upper right", fontsize=9)
        ax.grid(True, alpha=0.3)
        (line,) = ax.plot([], [], "b-", lw=1.0)

        def update(_):
            if smv_buf:
                line.set_data(time_buf[-500:], smv_buf[-500:])
                ax.relim()
                ax.autoscale_view(scalex=True, scaley=False)
            return (line,)

        ani = animation.FuncAnimation(fig, update, interval=100,
                                       blit=False, cache_frame_data=False)
        plt.show(block=False)

    try:
        while True:
            raw = ser.readline()
            if not raw:
                if do_plot:
                    import matplotlib.pyplot as plt
                    plt.pause(0.01)
                continue

            try:
                line = raw.decode("utf-8", errors="replace").rstrip()
            except Exception:
                continue

            ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]

            m = IMU_RE.search(line)
            if m:
                ax_, ay_, az_ = float(m[1]), float(m[2]), float(m[3])
                gx_, gy_, gz_ = float(m[4]), float(m[5]), float(m[6])
                smv = (ax_**2 + ay_**2 + az_**2) ** 0.5
                if csv_writer:
                    csv_writer.writerow([ts, ax_, ay_, az_, gx_, gy_, gz_,
                                         f"{smv:.4f}"])
                    csv_file.flush()
                t_now = time.monotonic() - t0
                smv_buf.append(smv)
                time_buf.append(t_now)
                continue   # don't echo raw IMU lines to terminal

            m = STATE_RE.search(line)
            if m:
                new_state = m[2]
                color = STATE_COLORS.get(new_state, ANSI_RESET)
                print(f"{ts}  {color}{line}{ANSI_RESET}")
                continue

            if "FALL CONFIRMED" in line:
                print(f"{ts}  {ANSI_RED}{'!'*40}{ANSI_RESET}")
                print(f"{ts}  {ANSI_RED}{line}{ANSI_RESET}")
                print(f"{ts}  {ANSI_RED}{'!'*40}{ANSI_RESET}")
                continue

            m = CONF_RE.search(line)
            if m:
                fall_p = float(m[1])
                color  = ANSI_RED if fall_p > 0.75 else ANSI_YELLOW
                print(f"{ts}  {color}{line}{ANSI_RESET}")
                continue

            print(f"{ts}  {line}")

            if do_plot:
                import matplotlib.pyplot as plt
                plt.pause(0.001)

    except KeyboardInterrupt:
        print("\nDisconnected")
    finally:
        ser.close()
        if csv_file:
            csv_file.close()
            print(f"Saved capture to {capture_path}")


def main() -> None:
    ap = argparse.ArgumentParser(description="Fall detection device monitor")
    ap.add_argument("--port",    required=True, help="Serial port")
    ap.add_argument("--baud",    type=int, default=115200)
    ap.add_argument("--capture", help="CSV file to save raw IMU data")
    ap.add_argument("--plot",    action="store_true", help="Live SMV plot")
    args = ap.parse_args()
    monitor(args.port, args.baud, args.capture, args.plot)


if __name__ == "__main__":
    main()
