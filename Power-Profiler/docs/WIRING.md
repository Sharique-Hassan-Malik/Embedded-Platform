# Wiring Guide

## Bill of Materials

| Qty | Component | Notes |
|---|---|---|
| 1 | Arduino Uno or Nano (profiler) | ATmega328P — the measurement instrument |
| 1 | Arduino Uno or Nano (DUT) | Device under test — the target being profiled |
| 1 | INA219 breakout module | Adafruit #904 or equivalent |
| 1 | 0.1 Ω 1% resistor (shunt) | Wirewound, ≥ 1 W (or use INA219 module's built-in shunt) |
| 4 | Jumper wires | Annotation pin connections DUT ↔ profiler |
| 1 | Shared GND wire | DUT GND ↔ profiler GND — mandatory |

---

## Profiler Arduino — INA219 Wiring

The INA219 communicates over I2C. On the Uno/Nano, I2C is on A4 (SDA) and
A5 (SCL). These are shared with the INA219; no additional configuration needed.

```
Profiler Arduino       INA219 module
─────────────────────────────────────
 A4 (SDA)      ────── SDA
 A5 (SCL)      ────── SCL
 3.3V or 5V    ────── VCC  (check module label — most accept 3–5.5 V)
 GND           ────── GND
```

The INA219 module on Adafruit #904 includes the shunt resistor (0.1 Ω) on its
board. Use the V+ and V- terminals as the current sense inputs.

---

## Shunt Resistor Insertion (Current Sense Path)

The shunt must be placed **in series** with the DUT's power supply rail.
Current flows from the supply through the shunt into the DUT:

```
Power supply (+)
      │
    [Shunt 0.1 Ω]
      │
    INA219 V+
    INA219 V−
      │
    DUT VCC
      │
   (DUT load)
      │
    DUT GND ─────── Profiler GND ─────── Power supply (−)
```

The INA219 module wired to the Adafruit breakout:

```
Power supply (+) ──── INA219 Vin+
INA219 Vin−      ──── DUT VCC
```

The 0.1 Ω shunt on the breakout sits between Vin+ and Vin−. Current flows
from Vin+ through the shunt to Vin−, then to the DUT.

---

## Annotation Pin Connections

Connect each annotation channel from the DUT to the profiler with a single wire.
Both boards must share a common GND.

```
DUT Arduino D2 ────── Profiler Arduino D2   (channel 0)
DUT Arduino D3 ────── Profiler Arduino D3   (channel 1)
DUT Arduino D4 ────── Profiler Arduino D4   (channel 2)
DUT Arduino D5 ────── Profiler Arduino D5   (channel 3)
DUT GND        ────── Profiler GND          (mandatory)
```

The profiler enables `INPUT_PULLUP` on all annotation pins; the DUT drives them
as `OUTPUT`. Idle (unannotated) time appears in the trace without any channel
highlight.

---

## Direct ADC Path (No INA219)

If no INA219 is available, set `USE_INA219 0` in `config.h` and wire a simple
voltage divider across the shunt to Arduino A0:

```
DUT VCC ────[shunt 0.1 Ω]──── DUT GND
                  │
               [op-amp ×10 or voltage divider]
                  │
              Profiler A0
```

A non-inverting op-amp with gain 10 (e.g. LM358) amplifies the 0–40 mV shunt
voltage to 0–400 mV, then a 10× attenuator brings it to 0–40 mV within the
0–5 V ADC range. Alternatively skip the amplifier and accept lower resolution
at low currents.

Resolution without amplification at 0.1 Ω shunt, 5 V reference:
  1 LSB (10-bit) = 5000 mV / 1024 = 4.88 mV
  4.88 mV / 0.1 Ω = 48.8 mA per LSB — only useful for devices drawing > 100 mA.

With ×10 amplification:
  4.88 mA per LSB — suitable for 5–500 mA range.

The INA219 path gives 12-bit resolution and ±40 mV range:
  10 µV per LSB / 100 mΩ = 0.1 mA per LSB — recommended for all precision work.

---

## Quick Start

1. Flash `firmware/power_profiler/power_profiler.ino` to the profiler Arduino.
2. Flash `firmware/example_dut/example_dut.ino` to the DUT Arduino.
3. Wire INA219, shunt and annotation pins as above.
4. Connect the profiler Arduino to the PC via USB.

```bash
pip install -r host/requirements.txt
python host/main.py --port /dev/ttyACM0 --supply 3300
```

The dashboard opens immediately. Press the DUT reset button to observe the
startup current spike, followed by periodic annotation-marked sections.

---

## Calibration

The default `SHUNT_MOHM = 100` (0.1 Ω). If your actual shunt resistance
differs (measure with a 4-wire ohmmeter), update this constant in `config.h`
before flashing.

INA219 gain (`INA219_PG`):
| Value | Shunt voltage range | Max current at 0.1 Ω |
|---|---|---|
| 0 | ±40 mV | ±400 mA |
| 1 | ±80 mV | ±800 mA |
| 2 | ±160 mV | ±1.6 A |
| 3 | ±320 mV | ±3.2 A |

Choose the smallest range that covers your DUT's peak current for maximum
resolution.
