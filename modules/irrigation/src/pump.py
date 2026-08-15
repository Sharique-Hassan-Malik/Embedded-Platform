# pump.py — relay-controlled water pump driver.
# Enforces a minimum cooldown between cycles to protect the pump motor.

from machine import Pin
import time
import config


class Pump:
    """
    Controls a relay-switched water pump.

    The relay coil is energised by driving the GPIO pin HIGH (active-HIGH
    relay module). The pump runs for PUMP_ON_SEC seconds per cycle.
    A cooldown of PUMP_COOLDOWN_SEC prevents back-to-back cycles.
    """

    def __init__(self) -> None:
        self._pin          = Pin(config.RELAY_PIN, Pin.OUT, value=0)
        self._last_run_ms  = 0        # millis() of last pump stop
        self._total_cycles = 0

    @property
    def total_cycles(self) -> int:
        return self._total_cycles

    def is_cooling_down(self) -> bool:
        elapsed = time.ticks_ms() - self._last_run_ms
        return time.ticks_diff(elapsed, 0) < config.PUMP_COOLDOWN_SEC * 1000

    def run(self) -> bool:
        """
        Activate the pump for PUMP_ON_SEC seconds.
        Returns True if the pump ran or False if still in cooldown.
        """
        if self._last_run_ms != 0 and self.is_cooling_down():
            return False

        self._pin.on()
        time.sleep(config.PUMP_ON_SEC)
        self._pin.off()

        self._last_run_ms = time.ticks_ms()
        self._total_cycles += 1
        return True

    def force_off(self) -> None:
        """Emergency stop — turn relay off immediately."""
        self._pin.off()
