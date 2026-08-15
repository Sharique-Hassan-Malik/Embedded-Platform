# Architecture

## Overview

```
main.py
  │
  ├── rtc.py        — DS3231 I2C driver (BCD decode, temperature)
  ├── moisture.py   — ADC oversampling, DRY/WET calibration → moisture %
  ├── pump.py       — relay GPIO, PUMP_ON_SEC timer, cooldown guard
  ├── rules.py      — threshold + window evaluation, JSON persistence
  ├── logger.py     — CSV append to LittleFS, trim when full
  ├── display.py    — SSD1306 dashboard renderer (uses oled.py)
  ├── oled.py       — SSD1306 I2C driver, 5×7 font, framebuffer
  └── repl.py       — non-blocking serial command interface
```

All modules carry zero external MicroPython library dependencies except
`machine` (hardware I/O) and standard library modules (`json`, `os`, `time`).
The SSD1306 driver, DS3231 driver and 5×7 ASCII font are implemented from
scratch.

---

## Main Loop

The main loop has no blocking `sleep()`. All periodic tasks are gated by
`time.ticks_diff()` comparisons so the serial REPL remains responsive:

```
while True:
  │
  ├── every CHECK_INTERVAL_SEC:
  │     read moisture sensors
  │     evaluate rules (threshold + window)
  │     if should_irrigate → pump.run() → log
  │
  ├── every 2 s:
  │     display.show()   (OLED refresh)
  │
  ├── every loop iteration (~20 ms):
  │     repl.poll()      (read one char from stdin)
  │
  └── time.sleep_ms(20)
```

---

## Sensor Signal Chain

```
Capacitive soil probe
  │  (RC oscillator frequency → voltage)
  ▼
RP2040 ADC (16-bit, read_u16())
  │  64-sample oversampling → average
  ▼
Linear calibration map:
  moisture% = (DRY_ADC − raw) / (DRY_ADC − WET_ADC) × 100
  clamped to [0, 100]
```

Calibration constants (DRY_ADC and WET_ADC) can be updated at runtime via
the REPL (`set dry N` / `set wet N`) without reflashing.

---

## Rule Engine

`rules.should_irrigate(moisture_pct, hour)` returns `(bool, reason_string)`.

Three conditions must all be true:

```
1. rules.enabled == True
2. moisture_pct <= threshold
3. hour ∈ any [start, end) watering window
```

Rule state persists across reboots in `/rules.json` on flash.

---

## DS3231 Register Layout

```
Register  Content       Format
0x00      Seconds       BCD 00–59
0x01      Minutes       BCD 00–59
0x02      Hours         BCD 00–23  (bit 6 = 0 selects 24-hour mode)
0x03      Day of week   1–7
0x04      Date          BCD 01–31
0x05      Month         BCD 01–12
0x06      Year          BCD 00–99  (+ 2000)
0x11      Temp MSB      signed int8 °C
0x12      Temp LSB      bits [7:6] × 0.25 °C
```

BCD encode/decode is implemented in `rtc.py` without any library.

---

## SSD1306 I2C Protocol

All commands are prefixed with control byte `0x00`.
All data (GDDRAM writes) are prefixed with control byte `0x40`.

Initialization sequence sets:
- Horizontal addressing mode (automatic column and page wrap)
- Internal charge pump enabled (no external VLED supply needed)
- Column remapping and COM scan direction for correct orientation

`show()` sets column and page address windows then sends the full
128 × 8-page framebuffer (1024 bytes) in 16-byte chunks to respect
the I2C controller's internal buffer limit.

---

## Flash Storage

| Path | Content |
|---|---|
| `/log.csv` | Timestamped CSV: timestamp, sensor, moisture_pct, pump_fired, note |
| `/rules.json` | Persisted rule state: threshold, windows, enabled |

`logger.py` trims the log when it reaches `LOG_MAX_ROWS` (2000 rows) by
discarding the oldest half, keeping storage use bounded on the RP2040's
2 MB flash.

---

## Serial REPL

`repl.poll()` is called every 20 ms. It reads one character from `sys.stdin`
without blocking. When a newline is received the accumulated line is dispatched
to `_handle()`.

Available commands are printed by `help`. All configuration changes call
`rules._save()` which writes `/rules.json` immediately, so settings survive
a power cycle.
