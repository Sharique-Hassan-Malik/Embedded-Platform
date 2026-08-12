# Wiring Guide

## Bill of Materials

| Qty | Component | Notes |
|---|---|---|
| 1 | Arduino Uno or Nano | ATmega328P |
| 1 | Hall-effect sensor (A3144 or SS49E) | Digital output, active low |
| 1 | Small neodymium magnet | Glued to rotor arm |
| 1 | 74HC595 shift register | 8-bit SIPO, 5 V compatible |
| 8 | 5 mm LEDs (any colour) | |
| 8 | 68 Ω resistors | Current limiting for LEDs (5 V, ~40 mA GPIO limit) |
| 1 | DC motor 300–2000 RPM | Brushed, 5–12 V |
| 1 | Motor driver (L298N or L9110S) | Protects Arduino from motor noise |
| 1 | Tactile button | Image select |
| 1 | 10 kΩ resistor | Button pull-down (optional — firmware uses internal pull-up) |
| — | Slip-ring connector (optional) | Eliminates wire twist on continuous rotation |
| — | Perfboard + standoffs | For the spinning arm |

---

## Hall Sensor (INT0 — D2)

The A3144 is an open-collector digital Hall sensor: output goes LOW when the
magnet's south pole approaches and returns HIGH when removed. Wire it so the
Arduino sees a RISING edge on D2 once per revolution.

```
            A3144
            ┌────────┐
  5V ───────┤ VCC    │
  GND ──────┤ GND    │
  D2  ──────┤ OUT    ├──── 10 kΩ ──── 5V  (external pull-up)
            └────────┘
```

Placement: mount the sensor on the stationary frame; glue the magnet to the
spinning arm so it sweeps past the sensor face each revolution. Keep the air
gap below 5 mm for reliable switching.

Orientation: the A3144 triggers on the south pole face. Try both magnet
orientations if the display does not stabilise — the sensor will pass through
HIGH only if it sees the correct pole.

---

## 74HC595 Shift Register

```
74HC595 pin         Arduino pin
─────────────────────────────────
 QA–QH (outputs)   → LED anodes (via 68 Ω resistors to GND cathode side)
 SER (data in)     → D11
 SRCLK (shift clk) → D13
 RCLK  (latch)     → D10
 OE    (output en) → GND  (always enabled — active low)
 SRCLR (clear)     → 5V   (active low clear disabled)
 VCC               → 5V
 GND               → GND
```

Each LED connects from a Q pin through a 68 Ω resistor to GND. At 5 V this
gives roughly (5 − 2) / 68 ≈ 44 mA per LED when on. The 74HC595 is rated for
35 mA per output and 70 mA total current — use 100 Ω resistors if running all
8 LEDs simultaneously or drive the strip in segments with a transistor buffer.

---

## LED Strip Physical Layout

Mount LEDs in a straight line along the spinning arm, spaced equally between
the rotation axis (innermost) and the arm tip (outermost).

```
Axis                                              Arm tip
  │                                                  │
  ○──── LED0 ── LED1 ── LED2 ── LED3 ── LED4 ──… LED7
  │
  Motor shaft
```

Bit 0 of each image column corresponds to LED0 (innermost); bit 7 to LED7
(outermost). Reverse the bit order in `image.h` if your strip is wired
the other way.

---

## Image Select Button

```
D4 ─────── [button] ─────── GND
```

The firmware enables the ATmega's internal 20 kΩ pull-up on D4. No external
resistor is needed. Pressing the button advances to the next image in
`IMAGE_TABLE`. A 30 ms software debounce prevents multiple triggers.

---

## Motor and Power

Power the motor and Arduino from separate supplies or use the L298N's 5 V
regulated output for the Arduino. Motor switching noise can reset the
microcontroller if sharing a supply without decoupling.

Add a 100 µF electrolytic capacitor across the motor terminals and a 100 nF
ceramic across the Arduino's 5V/GND pins to suppress motor noise.

---

## Assembly Tips

1. Balance the spinning arm to reduce vibration — unbalanced loads at 600+ RPM
   generate significant vibration that can shift the Hall trigger angle.

2. Use a slip ring if you want to power the LEDs and Arduino from a stationary
   supply. Alternatively, mount a small LiPo and voltage regulator on the rotor
   and transmit only the Hall signal and button control statically.

3. Start with `RPM_MIN = 200` and spin the motor slowly. Open the Serial Monitor
   at 115200 baud and verify the RPM reading increments before full speed.

4. The image will appear to drift angularly if the Hall magnet is not at the
   same angular position each revolution — check that the magnet is secured and
   the sensor mount is rigid.
