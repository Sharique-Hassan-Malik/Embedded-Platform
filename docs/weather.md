# Architecture

## Overview

A battery-powered weather station built on the PIC18F26K22 that reads
temperature, humidity and pressure from a BME280 over I2C, timestamps
readings with a DS3231 RTC, renders a summary screen on a 2.9-inch SSD1680
e-ink display and appends CSV records to an SD card.  The microcontroller
spends the vast majority of its time in Sleep mode, waking only when the
DS3231 Alarm 2 interrupt fires.

No RTOS, no HAL and no FAT library are used.  Every driver is written from
scratch in C.

---

## Power Cycle

```
Sleep (31 kHz LFINTOSC)
  MCU  : < 0.1 µA
  DS3231: 1.5 µA (crystal oscillator, alarm armed)
  BME280: 0.1 µA (sleep mode)
  SSD1680: < 1 µA (deep sleep, image retained in RAM)
  SD    : < 0.2 µA (power-down state)
  Total : ~3 µA
      │
      │  DS3231 Alarm 2 fires (RB0 IOC)
      ▼
Wake (1 MHz HFINTOSC, ~3 s active)
  1. osc_1mhz()             ~0.1 ms
  2. bme280_read()          ~10 ms,  0.7 mA MCU + 0.72 mA BME280
  3. ds3231_read()          ~1 ms
  4. epd_init()             ~25 ms,  0.7 mA MCU + 5 mA EPD
  5. render_display()       ~2 ms   (framebuffer only, no SPI)
  6. epd_full_refresh()     ~2 500 ms, 0.7 mA MCU + 26 mA EPD
  7. epd_deep_sleep()       ~1 ms
  8. sdlog_write()          ~150 ms, 0.7 mA MCU + 100 mA SD write peak
  9. ds3231_clear_alarm()   ~1 ms
 10. osc_lfintosc() → SLEEP()
      │
      └── back to Sleep
```

**Energy per cycle at 5-minute interval (3.3 V supply):**

| Sub-system | Current × Time | Charge |
|---|---|---|
| MCU @ 1 MHz | 0.7 mA × 2.7 s | 1.9 mC |
| EPD initialise + refresh | 20 mA avg × 2.5 s | 50 mC |
| BME280 measurement | 0.72 mA × 0.01 s | 0.007 mC |
| SD write | 40 mA avg × 0.15 s | 6 mC |
| Sleep (5 min) | 3 µA × 300 s | 0.9 mC |
| **Total** | | **~59 mC** |

At 5-minute intervals: 59 mC / 300 s = **0.20 mA average current**.
Two AA cells (3 000 mAh × 3.3 V LDO efficiency ≈ 2 400 mAh usable) last
approximately **500 days** — well over the target of months.

The e-ink panel is the dominant energy consumer because of its 2.5-second
full-refresh waveform.  Reducing the update interval to 15 minutes extends
battery life proportionally.

---

## Module Descriptions

### `i2c.c` — MSSP1 I2C master

Hardware I2C at 100 kHz (SSPADD = 1 at Fosc = 1 MHz).  All operations are
blocking.  `i2c_reg_read()` uses a repeated start between the write phase
(register address) and the read phase, as required by BME280 and DS3231.

### `bme280.c` — BME280 sensor

Uses forced mode: the sensor takes one measurement and returns to sleep
automatically.  Trimming coefficients are read once in `bme280_init()` and
stored in static RAM.  The Bosch integer compensation formulas are applied
verbatim, using `int64_t` arithmetic only for the pressure calculation.

Fixed-point output scales:

| Quantity | Units | Example |
|---|---|---|
| Temperature | 0.01 °C | 2415 = 24.15 °C |
| Pressure | Pa (whole) | 96386 = 963.86 hPa |
| Humidity | %RH × 1024 | 66560 = 65.0 %RH |

### `ds3231.c` — DS3231 RTC

The DS3231 /INT pin is configured as an open-drain alarm interrupt.
Alarm 2 is used (Alarm 1 is left inactive) because Alarm 2's minimum
resolution is one minute, which matches the measurement interval.

To fire every N minutes (`WAKE_INTERVAL_MIN`):
- If N > 1: set Alarm 2 minute register to N with mask bit 0 and all other
  mask bits 1 (hour and day ignored).  The alarm fires when minutes = N.
- If N = 0 or 1: all three mask bits set → alarm fires every minute.

After each measurement the alarm flag must be cleared via `ds3231_clear_alarm()`
before the next alarm can assert /INT.

### `spi.c` — MSSP2 SPI master

Shared SPI bus at Fosc/16 = 62.5 kHz.  CS management is entirely the
responsibility of the caller.  Both devices must have CS deasserted when
not in use.  The bus is fast enough for both the e-ink controller (no
maximum clock) and SD card initialisation (< 400 kHz required until ACMD41
completes; higher speeds are permitted after initialisation but 62.5 kHz
is adequate for the infrequent 512-byte sector writes here).

### `epd.c` — SSD1680 e-ink driver

The SSD1680 is initialised from reset on every wake cycle because it is put
into deep sleep mode after each refresh.  Deep sleep consumes < 1 µA and
retains the displayed image without power.

Framebuffer `epd_buf[16][37]` stores the 296 × 128 image as 16 pages of 37
bytes (8 columns per byte, MSB = leftmost pixel, 1 = white, 0 = black).
`epd_full_refresh()` writes the complete buffer and triggers the full update
waveform, which takes approximately 2.5 seconds.

Text rendering uses the shared 5×7 column-major font table.

### `sdlog.c` — Raw-sector CSV logger

No FAT library is used.  Records are written to contiguous 512-byte sectors
starting at sector 2048 (skipping the MBR and any FAT structures that might
be present on a pre-formatted card).

**Record format (64 bytes, space-padded to sector boundary):**
```
2024-06-15 08:30:00 +24.15C 065%RH 963.8hPa\n           (padded to 64 B)
```

Eight records fit per 512-byte sector.  The current position (sector offset
from `SDLOG_START_SECTOR` and record index within sector) is saved to EEPROM
after each write.  On the next wake the firmware reads the saved position,
reads the current partial sector, fills in the next record and writes the
sector back.

**Recovery on a host:**
```bash
dd if=/dev/sdX bs=512 skip=2048 count=64000 | grep -a "^20" > weather.csv
```

### `main.c` — Application orchestrator

The PIC18 spends most of its time in `SLEEP()` with LFINTOSC (31 kHz) as the
oscillator, giving a core current of < 0.1 µA.  The DS3231 Alarm 2 interrupt
falls on RB0, triggering interrupt-on-change and waking the MCU.

On wake the oscillator is switched to HFINTOSC at 1 MHz before any peripheral
access.  After all tasks complete, the oscillator is switched back to LFINTOSC
before the next `SLEEP()` instruction.

---

## Hardware Schematic

```
PIC18F26K22                BME280             DS3231
───────────────────        ─────────          ────────────
RC3 (SCL1) ─────────────► SCL                SCL  ◄──── RC3
RC4 (SDA1) ─────────────► SDA                SDA  ◄──── RC4
VDD ─── 4.7 kΩ ─── RC3 (I2C pull-up)
VDD ─── 4.7 kΩ ─── RC4
SDO (BME280) ─── GND        (I2C addr 0x76)

DS3231 /INT ─── 10 kΩ ─── VDD
            └───────────────────────────────► RB0 (IOC wake)

SPI bus (shared):
RB1 (SCK2) ─────────────────────────────────► EPD SCK / SD CLK
RB2 (SDO2) ─────────────────────────────────► EPD MOSI / SD DI
RB3 (SDI2) ◄────────────────────────────────── EPD (unused) / SD DO

E-ink (SSD1680 module):
RA0 (EPD_CS)   ─────────────────────────────► /CS
RA1 (EPD_DC)   ─────────────────────────────► DC
RA2 (EPD_RST)  ─────────────────────────────► RST
RA3 (EPD_BUSY) ◄───────────────────────────── BUSY

SD card (SPI mode):
RA4 (SD_CS)    ─────────────────────────────► /CS
               VDD ─── 10 kΩ ─── SD MISO    (pull-up)

RC0 (LED) ─── 330 Ω ─── LED(+) ─── GND

Power:
  3 × AA cells (4.5 V) ─── MCP1700 3.3 V LDO ─── VDD
  100 µF + 100 nF decoupling at LDO output
  100 nF ceramic at each VDD pin of PIC18
```

---

## File Map

| File | Description |
|---|---|
| `firmware/hw.h` | Pin macros, I2C addresses, EEPROM layout, log constants |
| `firmware/i2c.c/.h` | MSSP1 hardware I2C master at 100 kHz |
| `firmware/bme280.c/.h` | BME280 forced-mode driver, Bosch integer compensation |
| `firmware/ds3231.c/.h` | DS3231 RTC driver, Alarm 2, timestamp |
| `firmware/spi.c/.h` | MSSP2 hardware SPI master (shared e-ink + SD bus) |
| `firmware/epd.c/.h` | SSD1680 2.9-inch e-ink driver, framebuffer, text |
| `firmware/sdlog.c/.h` | Raw-sector CSV SD logger, EEPROM position tracking |
| `firmware/font.c/.h` | 5×7 column-major bitmap font (64 ASCII glyphs) |
| `firmware/main.c` | Power cycle orchestration, display layout, format helpers |
| `docs/ARCHITECTURE.md` | This document |
