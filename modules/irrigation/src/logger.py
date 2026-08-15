# logger.py — timestamped CSV log to RP2040 internal flash (LittleFS).
# Appends one row per event. When the row count exceeds LOG_MAX_ROWS the
# oldest half of the file is discarded to keep storage use bounded.

import os
import config


_HEADER = "timestamp,sensor,moisture_pct,pump_fired,note\n"


def _ensure_file() -> None:
    try:
        os.stat(config.LOG_FILE)
    except OSError:
        with open(config.LOG_FILE, "w") as f:
            f.write(_HEADER)


def _count_rows() -> int:
    try:
        with open(config.LOG_FILE, "r") as f:
            return sum(1 for _ in f) - 1   # subtract header
    except OSError:
        return 0


def _trim() -> None:
    """Discard the oldest half of the log, keeping the header intact."""
    try:
        with open(config.LOG_FILE, "r") as f:
            lines = f.readlines()
    except OSError:
        return

    header = lines[0] if lines else _HEADER
    data   = lines[1:]
    keep   = data[len(data) // 2:]   # keep the newer half

    with open(config.LOG_FILE, "w") as f:
        f.write(header)
        for line in keep:
            f.write(line)


def log(timestamp: str, sensor: int, moisture_pct: float,
        pump_fired: bool, note: str = "") -> None:
    """Append one CSV row. Trims oldest entries if LOG_MAX_ROWS is exceeded."""
    _ensure_file()

    if _count_rows() >= config.LOG_MAX_ROWS:
        _trim()

    row = "{},{},{:.1f},{},{}\n".format(
        timestamp,
        sensor,
        moisture_pct,
        1 if pump_fired else 0,
        note.replace(",", ";"),   # prevent CSV injection
    )
    with open(config.LOG_FILE, "a") as f:
        f.write(row)


def tail(n: int = 20) -> list:
    """Return the last n rows as a list of strings (no header)."""
    _ensure_file()
    try:
        with open(config.LOG_FILE, "r") as f:
            lines = f.readlines()
        return [l.rstrip() for l in lines[1:] if l.strip()][-n:]
    except OSError:
        return []


def clear() -> None:
    """Erase the log file and write a fresh header."""
    with open(config.LOG_FILE, "w") as f:
        f.write(_HEADER)
