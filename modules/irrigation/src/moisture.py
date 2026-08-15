# moisture.py — capacitive soil moisture sensor reader.
# Reads up to 4 sensors via the RP2040 ADC (16-bit mode).
# Applies oversampling for noise reduction and maps raw ADC to moisture %.

from machine import ADC, Pin
import config


class MoistureSensor:
    """
    Single capacitive moisture sensor on one ADC pin.

    Capacitive sensors output a voltage inversely proportional to soil moisture:
      dry soil  → high voltage → high ADC count (≈ DRY_ADC)
      wet soil  → low voltage  → low ADC count  (≈ WET_ADC)

    moisture_pct() maps this to 0 (dry) … 100 (saturated) linearly.
    Readings outside the calibrated range are clamped to [0, 100].
    """

    def __init__(self, pin: int) -> None:
        self._adc     = ADC(Pin(pin))
        self._dry     = config.DRY_ADC
        self._wet     = config.WET_ADC
        self._n       = config.OVERSAMPLE_N

    def _raw(self) -> int:
        """Oversampled 16-bit ADC reading."""
        total = 0
        for _ in range(self._n):
            total += self._adc.read_u16()
        return total // self._n

    def raw(self) -> int:
        return self._raw()

    def moisture_pct(self) -> float:
        """Return soil moisture as a percentage (0.0 = dry, 100.0 = saturated)."""
        adc = self._raw()
        # Linear interpolation: dry end → 0%, wet end → 100%.
        # ADC decreases as moisture increases, so subtract from dry end.
        span = self._dry - self._wet
        if span == 0:
            return 0.0
        pct = (self._dry - adc) / span * 100.0
        return max(0.0, min(100.0, pct))

    def calibrate(self, dry_adc: int, wet_adc: int) -> None:
        """Update calibration bounds at runtime."""
        self._dry = dry_adc
        self._wet = wet_adc


def read_all() -> list:
    """
    Read all connected sensors and return a list of (sensor_index, moisture_pct) tuples.
    Only SENSOR_COUNT sensors are read regardless of how many pins are defined.
    """
    results = []
    for i in range(config.SENSOR_COUNT):
        sensor = MoistureSensor(config.SENSOR_PINS[i])
        results.append((i, sensor.moisture_pct()))
    return results
