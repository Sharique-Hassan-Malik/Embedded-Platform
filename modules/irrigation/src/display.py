# display.py — OLED dashboard renderer.
# Updates the 128×64 display with the current sensor readings, pump status
# and last event. Layout is redrawn on each call to show().

from oled import SSD1306
import rules


class Display:
    """
    Dashboard layout (128×64 pixels, 5×7 font, 6 px per character):

      Line 0  (y=0):   HH:MM  YYYY-MM-DD
      Line 1  (y=10):  ─────────────────   (separator)
      Line 2  (y=12):  S0: XX.X%  S1: XX.X%
      Line 3  (y=22):  Threshold: XX%
      Line 4  (y=32):  Pump: IDLE / RUNNING
      Line 5  (y=42):  Cycles: NNNNN
      Line 6  (y=52):  Last: HH:MM fired/skip
    """

    def __init__(self) -> None:
        self._oled        = SSD1306()
        self._last_event  = "---"

    def set_last_event(self, msg: str) -> None:
        # Truncate to fit on one display line (max ~21 chars at 6 px/char).
        self._last_event = msg[:21]

    def show(self, dt: tuple, readings: list, pump_running: bool,
             pump_cycles: int) -> None:
        """
        dt       — (year, month, day, hour, minute, second, weekday) from RTC
        readings — list of (sensor_index, moisture_pct) tuples
        """
        oled = self._oled
        oled.fill(0)

        yr, mo, day, hr, mn, sc, _ = dt

        # ── Line 0: time and date ─────────────────────────────────────────────
        time_str = "{:02d}:{:02d}:{:02d}".format(hr, mn, sc)
        date_str = "{:04d}-{:02d}-{:02d}".format(yr, mo, day)
        oled.text(time_str, 0, 0)
        oled.text(date_str, 62, 0)

        # ── Separator ─────────────────────────────────────────────────────────
        oled.hline(0, 9, 128)

        # ── Moisture readings ─────────────────────────────────────────────────
        x = 0
        for idx, pct in readings[:4]:
            label = "S{}: {:.0f}%".format(idx, pct)
            oled.text(label, x, 11)
            x += 64

        # ── Threshold ─────────────────────────────────────────────────────────
        cfg = rules.get_config()
        oled.text("Thr:{:.0f}% {}".format(
            cfg["threshold"],
            "ON" if cfg["enabled"] else "OFF"
        ), 0, 21)

        # ── Pump status ───────────────────────────────────────────────────────
        status = "RUNNING" if pump_running else "IDLE   "
        oled.text("Pump:{}".format(status), 0, 31)

        # ── Cycle count ───────────────────────────────────────────────────────
        oled.text("Cycles:{:d}".format(pump_cycles), 0, 41)

        # ── Last event ────────────────────────────────────────────────────────
        oled.text(self._last_event, 0, 52)

        oled.show()
