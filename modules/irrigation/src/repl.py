# repl.py — non-blocking serial command REPL.
# Reads commands from USB serial without blocking the main loop.
# Commands are processed one per loop() iteration from the accumulated buffer.

import sys
import rules
import logger
import config as _cfg

_buf = ""

HELP = """
Commands:
  status          — print current sensor readings and rule state
  log [n]         — print last n log rows (default 20)
  log clear       — erase the log file
  set threshold N — set moisture threshold to N %
  set window H1 H2 — add watering window start H1, end H2 (24h)
  set windows clear — reset watering windows to empty
  enable          — enable the rule engine
  disable         — disable the rule engine (manual mode)
  set dry N       — set dry calibration ADC value for sensor 0
  set wet N       — set wet calibration ADC value for sensor 0
  help            — show this message
"""


def _handle(cmd: str, context: dict) -> None:
    """Process one command string. context provides live state references."""
    parts = cmd.strip().split()
    if not parts:
        return

    verb = parts[0].lower()

    if verb == "help":
        print(HELP)

    elif verb == "status":
        dt       = context["dt"]
        readings = context["readings"]
        cfg      = rules.get_config()
        print("Time : {:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}".format(*dt[:6]))
        for idx, pct in readings:
            print("S{} moisture: {:.1f}%".format(idx, pct))
        print("Threshold : {}%".format(cfg["threshold"]))
        print("Windows   : {}".format(cfg["windows"]))
        print("Enabled   : {}".format(cfg["enabled"]))
        print("Cycles    : {}".format(context["pump"].total_cycles))

    elif verb == "log":
        if len(parts) >= 2 and parts[1] == "clear":
            logger.clear()
            print("Log cleared.")
        else:
            n = int(parts[1]) if len(parts) >= 2 else 20
            rows = logger.tail(n)
            print("timestamp,sensor,moisture_pct,pump_fired,note")
            for r in rows:
                print(r)

    elif verb == "set" and len(parts) >= 3:
        key = parts[1].lower()
        if key == "threshold":
            rules.set_threshold(float(parts[2]))
            print("Threshold set to {}%".format(parts[2]))
        elif key == "window" and len(parts) >= 4:
            cfg = rules.get_config()
            wins = list(cfg["windows"])
            wins.append([int(parts[2]), int(parts[3])])
            rules.set_windows(wins)
            print("Window added: {}–{}".format(parts[2], parts[3]))
        elif key == "windows" and len(parts) >= 3 and parts[2] == "clear":
            rules.set_windows([])
            print("Watering windows cleared.")
        elif key == "dry":
            _cfg.DRY_ADC = int(parts[2])
            print("Dry ADC set to {}".format(parts[2]))
        elif key == "wet":
            _cfg.WET_ADC = int(parts[2])
            print("Wet ADC set to {}".format(parts[2]))
        else:
            print("Unknown setting.")

    elif verb == "enable":
        rules.set_enabled(True)
        print("Rule engine enabled.")

    elif verb == "disable":
        rules.set_enabled(False)
        print("Rule engine disabled.")

    else:
        print("Unknown command. Type 'help'.")


def poll(context: dict) -> None:
    """
    Call once per loop iteration. Accumulates characters from stdin without
    blocking. Processes a complete line when '\n' is received.
    """
    global _buf
    try:
        ch = sys.stdin.read(1)
    except Exception:
        return

    if ch in ("\n", "\r"):
        if _buf.strip():
            try:
                _handle(_buf, context)
            except Exception as e:
                print("Error:", e)
        _buf = ""
    else:
        _buf += ch
