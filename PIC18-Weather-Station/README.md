# pic18-weather-station

A battery-powered weather station on the PIC18F26K22 microcontroller.  A
BME280 sensor reads temperature, humidity and pressure over I2C.  A DS3231
RTC timestamps each reading and generates a periodic interrupt to wake the
MCU from sleep.  Readings are rendered on a 2.9-inch SSD1680 e-ink display
and appended as CSV records to a raw SD card sector log.  The firmware is
written entirely in C with no HAL, no FAT library and no RTOS.  The MCU
sleeps between measurements at under 3 µA total system current, targeting
months of runtime on a pair of AA cells.

---

## What it does

The system wakes every five minutes (configurable), takes a forced-mode BME280
measurement, reads the current timestamp from the DS3231, updates the e-ink
display with temperature, humidity, pressure, date and time, then writes a
64-byte CSV record to the SD card.  After completing these tasks the MCU
switches its oscillator to the 31 kHz LFINTOSC and enters Sleep.  The DS3231
Alarm 2 interrupt wakes it for the next cycle.

The e-ink display retains its image with zero power after each refresh —
the station shows the most recent reading indefinitely between updates.

---

## The hard part

**Deep sleep with alarm wake.** PIC18F26K22 has no built-in wake-on-timer
capable of minute-scale periods.  Instead, the DS3231 Alarm 2 register is
programmed to fire at a specific minute past each hour (or every minute).  The
/INT output is connected to RB0 and the interrupt-on-change module is enabled.
The MCU enters `SLEEP()` with LFINTOSC; the IOC event is the only wake source.
On wake the oscillator is switched back to 1 MHz HFINTOSC before any SPI or
I2C activity begins.

**E-ink full-refresh timing.** The SSD1680 takes approximately 2.5 seconds to
complete a full refresh.  `wait_busy()` polls the BUSY pin after triggering the
update waveform.  The BUSY pin stays high throughout the refresh; attempting
further SPI commands during this window corrupts the display.  After each
refresh `epd_deep_sleep()` puts the controller into its lowest-power state
while retaining the displayed image in on-chip RAM.

**SD logging without a filesystem.** Writing CSV to raw sectors avoids the RAM
and flash overhead of a FAT driver.  Records are 64 bytes, fixed-width and
space-padded so they pack exactly eight per 512-byte sector.  The current
write position (sector offset and intra-sector index) is stored in two EEPROM
locations and updated atomically after each write.  On a Linux host the log
can be extracted with a single `dd | grep` pipeline.

**BME280 integer compensation.** The Bosch compensation formulas use 32-bit
and 64-bit signed arithmetic with no floating point.  Temperature is a
prerequisite for pressure and humidity compensation (the `t_fine` variable
couples the calculations), so the three values must be computed in order.

---

## Architecture

See `docs/ARCHITECTURE.md` for the full power budget table, wake cycle
timeline, module descriptions, register-level notes for I2C/SPI/EEPROM and
a hardware schematic.

---

## Hardware

| Component | Part | Notes |
|---|---|---|
| MCU | PIC18F26K22 | 28-pin DIP or SOIC, 64 KB flash, 3.9 KB RAM |
| Sensor | BME280 | I2C address 0x76 (SDO to GND) |
| RTC | DS3231 | External 3 V coin cell for battery backup |
| Display | Waveshare 2.9-inch e-ink | SSD1680 controller, 296 × 128 B/W |
| Storage | Micro SD card | Class 4 or better, any capacity ≥ 64 MB |
| Regulator | MCP1700-3302 | 3.3 V LDO, quiescent 1.6 µA |
| Cells | 3 × AA | ~4.5 V input, ~500 days at 5-minute interval |

**Pin map**

| PIC18 pin | Signal |
|---|---|
| RC3 | I2C SCL (BME280 + DS3231) |
| RC4 | I2C SDA |
| RB1 | SPI SCK (e-ink + SD) |
| RB2 | SPI MOSI |
| RB3 | SPI MISO |
| RA0 | EPD /CS |
| RA1 | EPD DC |
| RA2 | EPD RST |
| RA3 | EPD BUSY (input) |
| RA4 | SD /CS |
| RB0 | DS3231 /INT (IOC wake) |
| RC0 | Status LED |

---

## Configuration

| Constant | Location | Default | Effect |
|---|---|---|---|
| `WAKE_INTERVAL_MIN` | `main.c` | 5 | Minutes between measurements |
| `TOUCH_DEFAULT_THR` | — | — | n/a |
| `SDLOG_START_SECTOR` | `hw.h` | 2048 | First sector used for logging |
| `SDLOG_MAX_SECTORS` | `hw.h` | 64000 | Log wraps after this many sectors |
| `BME280_ADDR` | `hw.h` | 0x76 | Change to 0x77 if SDO tied to VDD |

---

## Log recovery

```bash
# Extract all records from an SD card (Linux, raw device):
dd if=/dev/sdX bs=512 skip=2048 count=64000 | grep -a "^20" > weather.csv

# Or from an image file:
dd if=sd_image.bin bs=512 skip=2048 count=64000 | grep -a "^20" > weather.csv
```

Record format:
```
2024-06-15 08:30:00 +24.15C 065%RH 963.8hPa
```

---

## Building

1. Open MPLAB X and create a project for PIC18F26K22.
2. Select XC8 as the toolchain.
3. Add all `.c` files in `firmware/` to Source Files.
4. Add all `.h` files in `firmware/` to Header Files.
5. Confirm `_XTAL_FREQ = 1000000` matches the `osc_1mhz()` setting.
6. Build and program with PICkit 3/4 or SNAP.

No external libraries are required.

---

## Results

| Metric | Value |
|---|---|
| Active period per cycle | ~3 s |
| Sleep current (system) | ~3 µA |
| Average current at 5-min interval | ~0.20 mA |
| Estimated battery life (3 × AA) | ~500 days |
| Display refresh time | ~2.5 s (SSD1680 full update) |
| Measurement latency | ~10 ms (BME280 forced mode) |
| Log capacity at 5-min interval | ~1.8 years before sector wrap |
| Temperature accuracy | ±0.5 °C typical (BME280) |
| Pressure accuracy | ±1.0 hPa typical (BME280) |
| Humidity accuracy | ±3 %RH typical (BME280) |
