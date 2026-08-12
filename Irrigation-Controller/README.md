# Soil Moisture Irrigation Controller

An autonomous irrigation controller running MicroPython on the Arduino Nano
RP2040 Connect. Capacitive moisture sensors feed a rule engine that combines
a moisture threshold with configurable time-of-day watering windows. A relay
switches a small submersible pump; all events are timestamped via a DS3231 RTC
and logged to flash. A 128×64 OLED displays live readings and an interactive
serial REPL allows configuration without reflashing.

---

## The Hard Part

**All drivers are implemented from scratch.** The SSD1306 OLED driver in
`oled.py` initialises the display with the full hardware command sequence from
the datasheet (charge pump, addressing mode, COM scan direction), maintains a
1024-byte framebuffer and includes a complete 5×7 pixel ASCII font. The DS3231
RTC driver in `rtc.py` implements BCD encode/decode and direct I2C register
access. No MicroPython display or RTC library is used — only `machine.I2C` as
a raw byte transport.

**Non-blocking main loop.** The system has four concurrent periodic tasks —
sensor reads, rule evaluation, display refresh and serial REPL — all running
in a single-threaded loop with no blocking `sleep()`. Each task is gated by
`time.ticks_diff()` comparisons, keeping the REPL responsive at 20 ms
granularity even when sensor reads take tens of milliseconds.

**Bounded flash log with automatic trim.** `logger.py` appends timestamped
CSV rows to the RP2040's LittleFS filesystem. When the row count exceeds
`LOG_MAX_ROWS` (2000) the oldest half is discarded in a single rewrite pass.
This keeps storage use constant without requiring a circular buffer in RAM.

**Runtime-configurable rule engine with flash persistence.** Moisture threshold
and watering windows can be changed via the serial REPL. Changes are written
immediately to `/rules.json` on flash and reloaded on boot, so the controller
remembers its configuration across power cycles without reflashing.

---

## Architecture

```
main.py — event loop (non-blocking, 20 ms tick)
  ├── rtc.py        DS3231 BCD register driver
  ├── moisture.py   ADC oversampling + DRY/WET calibration
  ├── pump.py       relay GPIO + cooldown guard
  ├── rules.py      threshold + window logic + JSON persistence
  ├── logger.py     CSV append to LittleFS + auto-trim
  ├── display.py    OLED dashboard layout
  ├── oled.py       SSD1306 framebuffer + 5×7 font (from scratch)
  └── repl.py       non-blocking serial command interface
```

See `docs/ARCHITECTURE.md` for the full signal chain, rule evaluation logic
and flash storage layout.

---

## Hardware

| Component | Notes |
|---|---|
| Arduino Nano RP2040 Connect | MicroPython firmware |
| Capacitive soil moisture sensor | 3.3 V output (e.g. STEMMA, SEN0193) |
| DS3231 RTC module | I2C, coin-cell backup |
| SSD1306 OLED 128×64 | I2C |
| 5 V relay module | Active HIGH, optocoupler isolated |
| Small submersible pump | 3–6 V DC |

See `docs/WIRING.md` for pin assignments, I2C shared bus wiring and the RTC
initial-set procedure.

---

## Quickstart

### 1 — Flash MicroPython

Download the Arduino Nano RP2040 Connect UF2 from micropython.org. Hold
BOOTSEL while connecting USB, then drag the UF2 to the mass storage device.

### 2 — Upload source files

```bash
pip install mpremote
cd irrigation-controller/src
mpremote cp config.py rtc.py oled.py moisture.py pump.py \
            logger.py rules.py display.py repl.py main.py :
```

### 3 — Set the RTC (first boot only)

```python
# In mpremote repl (Ctrl+C to stop main.py first):
from rtc import DS3231
DS3231().set_time(2025, 6, 15, 8, 30, 0)
```

### 4 — Connect and run

Reset the board. Open a serial terminal at 115200 baud. Type `help` to see
available REPL commands.

---

## Serial REPL Commands

| Command | Effect |
|---|---|
| `status` | Print current readings and rule state |
| `log [n]` | Print last n log rows (default 20) |
| `log clear` | Erase the log file |
| `set threshold N` | Set moisture threshold to N % |
| `set window H1 H2` | Add watering window H1:00–H2:00 |
| `set windows clear` | Remove all watering windows |
| `enable` / `disable` | Enable or disable the rule engine |
| `set dry N` | Set dry calibration ADC value |
| `set wet N` | Set wet calibration ADC value |
| `help` | Show all commands |

---

## Log Format

`/log.csv` on the board's flash:

```
timestamp,sensor,moisture_pct,pump_fired,note
2025-06-15T06:03:00,0,32.4,1,pumped
2025-06-15T06:04:00,0,38.1,0,cooldown
2025-06-15T08:03:00,0,41.2,0,moisture ok (41.2% > 40%)
```

Read the log over USB with:
```bash
mpremote get /log.csv log.csv
```

---

## Configuration

All constants in `config.py`. Key parameters:

| Parameter | Default | Effect |
|---|---|---|
| `SENSOR_COUNT` | 1 | Number of sensors physically connected |
| `DRY_ADC` | 52000 | ADC reading in dry air (calibrate per sensor) |
| `WET_ADC` | 18000 | ADC reading in water |
| `PUMP_ON_SEC` | 10 | Seconds pump runs per irrigation cycle |
| `PUMP_COOLDOWN_SEC` | 300 | Minimum seconds between cycles |
| `MOISTURE_THRESHOLD` | 40 | Irrigate below this % |
| `WATERING_WINDOWS` | (6,8),(18,20) | Hours when irrigation is allowed |
| `CHECK_INTERVAL_SEC` | 60 | Rule evaluation frequency |
| `LOG_MAX_ROWS` | 2000 | Maximum log rows before trim |

---

## File Map

| File | Purpose |
|---|---|
| `src/main.py` | Entry point — event loop |
| `src/config.py` | Hardware pins and defaults |
| `src/rtc.py` | DS3231 I2C driver (BCD, temperature) |
| `src/oled.py` | SSD1306 framebuffer driver + 5×7 font |
| `src/moisture.py` | ADC oversampling and moisture % conversion |
| `src/pump.py` | Relay driver with cooldown guard |
| `src/rules.py` | Rule engine + JSON persistence |
| `src/logger.py` | CSV logger with auto-trim |
| `src/display.py` | OLED dashboard layout |
| `src/repl.py` | Non-blocking serial REPL |
| `docs/ARCHITECTURE.md` | Module diagram, signal chain, storage layout |
| `docs/WIRING.md` | BOM, pin table, calibration and deployment notes |
