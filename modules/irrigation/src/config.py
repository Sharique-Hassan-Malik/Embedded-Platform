# config.py — hardware pin assignments, sensor calibration and rule defaults.
# All values can be overridden at runtime via the serial REPL and are persisted
# to flash in config.json.

# ── Capacitive moisture sensors ───────────────────────────────────────────────
# Up to 4 sensors multiplexed through ADC pins.
# Each sensor is a resistor-capacitor oscillator whose output frequency
# decreases as soil moisture increases; the MCU reads the equivalent as a
# voltage on an analog pin (lower voltage = wetter soil for most modules).
#
# Calibration: DRY_ADC is the reading in completely dry air; WET_ADC is the
# reading with the sensor submerged in water. Moisture % is interpolated
# linearly between these bounds.
SENSOR_PINS    = (26, 27, 28, 29)   # GP26–GP29 = ADC0–ADC3 on RP2040
SENSOR_COUNT   = 1                  # how many sensors are physically connected
DRY_ADC        = 52000              # 16-bit ADC reading in dry air (~3.3 V)
WET_ADC        = 18000              # 16-bit ADC reading in water (~1.2 V)
OVERSAMPLE_N   = 64                 # averages to reduce noise

# ── Relay (pump) ──────────────────────────────────────────────────────────────
RELAY_PIN      = 6                  # GP6 — active HIGH to energise relay
PUMP_ON_SEC    = 10                 # seconds the pump runs per irrigation cycle
PUMP_COOLDOWN_SEC = 300             # minimum seconds between consecutive cycles

# ── RTC (DS3231 over I2C) ────────────────────────────────────────────────────
I2C_SDA        = 4                  # GP4
I2C_SCL        = 5                  # GP5
RTC_ADDR       = 0x68               # DS3231 default I2C address

# ── OLED display (SSD1306, 128×64, I2C) ─────────────────────────────────────
OLED_ADDR      = 0x3C               # most common SSD1306 address (alt: 0x3D)
OLED_WIDTH     = 128
OLED_HEIGHT    = 64

# ── Rule engine defaults ──────────────────────────────────────────────────────
# Irrigation fires when ALL enabled conditions are met:
#   moisture_pct <= MOISTURE_THRESHOLD   AND
#   current time is within an allowed watering window
MOISTURE_THRESHOLD = 40             # % — irrigate when soil is below this level
WATERING_WINDOWS   = [              # list of (start_hour, end_hour) in 24 h clock
    (6, 8),                         # 06:00–08:00
    (18, 20),                       # 18:00–20:00
]
CHECK_INTERVAL_SEC = 60             # rule evaluation frequency

# ── Flash log ─────────────────────────────────────────────────────────────────
LOG_FILE       = "/log.csv"         # on the RP2040's internal flash filesystem
LOG_MAX_ROWS   = 2000               # oldest rows dropped when limit reached
