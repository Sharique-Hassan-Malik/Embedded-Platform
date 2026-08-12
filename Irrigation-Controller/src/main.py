# main.py — irrigation controller entry point.
# Runs on the Arduino Nano RP2040 Connect under MicroPython.
#
# Boot sequence:
#   1. Load persisted rule configuration from flash.
#   2. Initialise RTC, OLED, pump relay and moisture sensors.
#   3. Enter the main loop: read sensors → evaluate rules → fire pump if needed
#      → update display → handle serial REPL commands.
#
# The main loop has no blocking sleep; time.ticks_ms() comparisons drive all
# periodic tasks so the serial REPL remains responsive at all times.

import time
import config
import rules
import logger
import moisture
import pump as pump_mod
import display as display_mod
import repl

try:
    from rtc import DS3231
    _rtc = DS3231()
    _has_rtc = True
except Exception:
    _has_rtc = False
    print("RTC not found — using uptime timestamps.")


def _timestamp(dt: tuple) -> str:
    if dt is None:
        return "{:.0f}".format(time.time())
    return "{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}".format(*dt[:6])


def _uptime_dt() -> tuple:
    """Fallback when no RTC: return epoch tuple anchored to 2000-01-01."""
    t = time.time()
    # MicroPython time.localtime() returns (year, month, mday, hour, min, sec,
    # weekday, yearday) — slice to match our 7-tuple convention.
    lt = time.localtime(t)
    return (lt[0], lt[1], lt[2], lt[3], lt[4], lt[5], lt[6])


def main() -> None:
    rules.load()

    pump     = pump_mod.Pump()
    disp     = display_mod.Display()
    readings = [(0, 0.0)]   # initial placeholder

    last_check_ms  = 0
    last_disp_ms   = 0
    display_period = 2_000   # ms between display refreshes

    print("Irrigation controller ready. Type 'help' for commands.")

    while True:
        now_ms = time.ticks_ms()
        dt     = _rtc.now() if _has_rtc else _uptime_dt()

        # ── Sensor read and rule evaluation (every CHECK_INTERVAL_SEC) ────────
        if time.ticks_diff(now_ms, last_check_ms) >= config.CHECK_INTERVAL_SEC * 1000:
            last_check_ms = now_ms
            readings      = moisture.read_all()

            # Use the average of all connected sensors for the rule check.
            avg_pct = sum(p for _, p in readings) / max(len(readings), 1)
            hour    = dt[3]

            fire, reason = rules.should_irrigate(avg_pct, hour)
            ts = _timestamp(dt)

            if fire:
                ran = pump.run()
                note = "pumped" if ran else "cooldown"
                disp.set_last_event("{} {}".format(ts[11:16], note))
                logger.log(ts, 0, avg_pct, ran, note)
                print("[{}] {} — {}".format(ts, note, reason))
            else:
                disp.set_last_event("{} skip".format(ts[11:16]))
                logger.log(ts, 0, avg_pct, False, reason)

        # ── OLED refresh ──────────────────────────────────────────────────────
        if time.ticks_diff(now_ms, last_disp_ms) >= display_period:
            last_disp_ms = now_ms
            try:
                disp.show(dt, readings,
                          pump_running=False,   # pump.run() is synchronous
                          pump_cycles=pump.total_cycles)
            except Exception as e:
                print("Display error:", e)

        # ── Serial REPL ───────────────────────────────────────────────────────
        repl.poll({
            "dt":       dt,
            "readings": readings,
            "pump":     pump,
        })

        time.sleep_ms(20)


main()
