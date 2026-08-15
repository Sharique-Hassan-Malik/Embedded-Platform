# rules.py — irrigation rule engine.
# Combines a moisture threshold test with time-of-day window filtering.
# Rules and thresholds are runtime-configurable and persisted to config.json.

import json
import os
import config as _cfg


# Runtime-mutable rule state (loaded from flash on startup).
_state = {
    "threshold":  _cfg.MOISTURE_THRESHOLD,
    "windows":    list(_cfg.WATERING_WINDOWS),
    "enabled":    True,
}

_RULE_FILE = "/rules.json"


def _load() -> None:
    try:
        with open(_RULE_FILE, "r") as f:
            saved = json.load(f)
        _state.update(saved)
    except (OSError, ValueError):
        pass   # use defaults on first boot or corrupt file


def _save() -> None:
    with open(_RULE_FILE, "w") as f:
        json.dump(_state, f)


def load():
    _load()


def should_irrigate(moisture_pct: float, hour: int) -> tuple:
    """
    Evaluate whether irrigation should fire.

    Returns (should_fire: bool, reason: str).

    All of the following must be true:
      1. Rule engine is enabled.
      2. moisture_pct <= threshold.
      3. Current hour falls within at least one watering window.
    """
    if not _state["enabled"]:
        return False, "disabled"

    if moisture_pct > _state["threshold"]:
        return False, "moisture ok ({:.1f}% > {}%)".format(
            moisture_pct, _state["threshold"])

    in_window = any(
        start <= hour < end
        for start, end in _state["windows"]
    )
    if not in_window:
        return False, "outside watering window (hour={})".format(hour)

    return True, "moisture {:.1f}% <= {}%, hour {} in window".format(
        moisture_pct, _state["threshold"], hour)


# ── Runtime configuration accessors ──────────────────────────────────────────

def set_threshold(pct: float) -> None:
    _state["threshold"] = float(pct)
    _save()


def set_windows(windows: list) -> None:
    """windows: list of [start_hour, end_hour] pairs."""
    _state["windows"] = windows
    _save()


def set_enabled(enabled: bool) -> None:
    _state["enabled"] = bool(enabled)
    _save()


def get_config() -> dict:
    return dict(_state)
