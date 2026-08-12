# Wiring Guide

## Bill of Materials

| Qty | Component | Notes |
|---|---|---|
| 1 | Arduino Nano RP2040 Connect | RP2040, MicroPython firmware |
| 1–4 | Capacitive soil moisture sensor | 3.3 V compatible (e.g. STEMMA, DFRobot SEN0193) |
| 1 | DS3231 RTC module | I2C, includes coin-cell backup battery |
| 1 | SSD1306 OLED 128×64 | I2C, 3.3 V or 5 V (check module label) |
| 1 | 5 V single-channel relay module | Active HIGH, optocoupler isolated |
| 1 | Submersible water pump | 3–6 V DC, ≤ 1 A |
| 1 | 3.3 V LDO or level shifter | If relay module requires 5 V signal |
| — | 20 cm silicone tubing | Pump to plant |

---

## Pin Assignments

| Signal | RP2040 Pin | Notes |
|---|---|---|
| Moisture sensor 0 | GP26 (ADC0) | Analog |
| Moisture sensor 1 | GP27 (ADC1) | Analog, if fitted |
| Moisture sensor 2 | GP28 (ADC2) | Analog, if fitted |
| Moisture sensor 3 | GP29 (ADC3) | Analog, if fitted |
| I2C SDA (RTC + OLED) | GP4 | Shared bus |
| I2C SCL (RTC + OLED) | GP5 | Shared bus |
| Relay control | GP6 | Digital output, active HIGH |

---

## I2C Shared Bus

The DS3231 (0x68) and SSD1306 (0x3C) share the same I2C0 bus on GP4/GP5.
Both use different addresses so no conflict occurs.
Add 4.7 kΩ pull-up resistors from SDA and SCL to 3.3 V if the modules do not
include them on-board (most breakout boards do).

```
GP4 ──── SDA ──── DS3231 SDA
              └── SSD1306 SDA
GP5 ──── SCL ──── DS3231 SCL
              └── SSD1306 SCL
3.3V ─[4.7kΩ]─ SDA  (only if no on-board pull-up)
3.3V ─[4.7kΩ]─ SCL
```

---

## Capacitive Moisture Sensor

Capacitive sensors have three pins: VCC, GND and AOUT (analog voltage).

```
Sensor VCC  → 3.3V
Sensor GND  → GND
Sensor AOUT → GP26 (ADC0)
```

Most 3.3 V capacitive sensors output roughly 2.8–3.3 V in dry soil and
1.2–1.8 V when submerged. Use the REPL to find your actual DRY_ADC and
WET_ADC values:

```
1. Insert sensor in dry air → type: log 1 → note the moisture_pct raw value
2. Submerge sensor in water → repeat
3. set dry <dry_reading>
4. set wet <wet_reading>
```

---

## Relay and Pump

The relay module switches the pump's 5 V supply:

```
RP2040 GP6 ──→ Relay IN   (3.3 V signal; check module specs)
5V external ──→ Relay VCC  (relay coil power)
GND         ──→ Relay GND

Relay COM ──→ Pump +
5V supply ──→ Relay NO    (Normally Open — closes when GP6 is HIGH)
GND       ──→ Pump −
```

If the relay module requires a 5 V logic signal on IN, use a 2N2222 NPN
transistor as a level shifter:
```
GP6 ──[1kΩ]──→ NPN Base
NPN Collector → Relay IN
NPN Emitter   → GND
5V            → Relay VCC
```

Limit pump runtime (`PUMP_ON_SEC`) so the reservoir does not overflow.
Start with 5–10 seconds and measure the delivered water volume.

---

## Flashing MicroPython

1. Hold BOOTSEL on the Nano RP2040 while connecting USB → it appears as a
   USB mass storage device.
2. Download `arduino_nano_rp2040_connect-*.uf2` from micropython.org.
3. Drag the UF2 file to the drive. The board reboots into MicroPython.
4. Use `mpremote` or Thonny to copy all files from `src/` to the board:

```bash
pip install mpremote
cd irrigation-controller/src
mpremote cp config.py rtc.py oled.py moisture.py pump.py \
            logger.py rules.py display.py repl.py main.py :
mpremote run main.py
```

Or in Thonny: open each file and use Files → Upload to /. Copy `main.py` last
so it is the entry point.

---

## RTC Initial Set

On first boot, set the RTC time via the serial REPL:

```python
# In Thonny shell or mpremote repl:
from rtc import DS3231
rtc = DS3231()
rtc.set_time(2025, 6, 15, 8, 30, 0)   # year, month, day, hour, min, sec
```

The DS3231 includes a coin-cell backup so the time persists through power cycles.

---

## Outdoor Deployment Notes

- Seal the electronics in a waterproof enclosure (IP65 box).
- Run only the sensor probe and pump tube outside the box.
- Use 3.3 V–rated capacitive sensors; resistive sensors corrode quickly.
- Keep the pump below the water reservoir level to avoid dry-running.
- Set `PUMP_COOLDOWN_SEC` to at least 5 minutes to protect the motor.
