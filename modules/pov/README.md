# Persistence of Vision Display

> Part of the [Embedded Platform](../../README.md) — nineteen firmware projects
> behind one build harness. This folder builds and runs on its own; `embed build
> --only pov` builds it alongside the rest.

A persistence-of-vision display on an Arduino Uno that renders stable mid-air
images from a spinning 8-LED strip. A Hall-effect sensor marks one fixed point
per revolution; Timer1 slices each revolution into 36 angular columns and
flashes the correct LED pattern at each position. The display continuously
adapts to motor speed variation so images stay sharp and stationary across the
full operating RPM range.

---

## The Hard Part

A naive POV implementation pre-computes column timing once and relies on the
motor running at a fixed speed. In practice, motor speed varies with load and
supply voltage, causing the image to warp, rotate or fragment.

This firmware measures every revolution period from the Hall sensor and
reprograms Timer1's compare register (OCR1A) at the start of each revolution.
Column 0 is always phase-locked to the Hall pulse by resetting TCNT1 to zero
inside the Hall ISR. The result: an image that appears rock-solid even as motor
speed drifts ±50% from its nominal value.

Three additional engineering decisions:

**Zero-jitter column timing using CTC mode.** Timer1 in Clear-Timer-on-Compare
mode reloads OCR1A automatically on each compare match — no software reload delay
that would accumulate phase error across 36 columns per revolution.

**Shift register bit-bang via direct port access.** `LEDs::write()` manipulates
`PORTB` directly instead of calling `digitalWrite()`. A full 8-bit transfer
takes ~1.1 µs, less than 0.3% of the shortest column period at 4000 RPM. This
keeps the Timer1 ISR's CPU budget well under 3 µs.

**Speed guard with automatic recovery.** If the Hall period falls outside the
`[RPM_MIN, RPM_MAX]` window — during spin-up, spin-down or a sudden load change —
the display blanks immediately and restarts automatically once the rotor
re-enters the window. No manual reset needed.

---

## Architecture

```
pov_display.ino
│
├── POV namespace (pov.h/.cpp)
│     ├── INT0 ISR      — Hall pulse, period measurement, Timer1 reprogram
│     └── TIMER1_COMPA  — column output, image column read from PROGMEM
│
├── LEDs namespace (leds.h/.cpp)
│     └── 74HC595 shift register, direct PORTB bit-bang
│
└── image.h
      └── 3 bitmapped images in PROGMEM (smiley, arrow, letter P)
```

See `docs/ARCHITECTURE.md` for interrupt interaction diagrams and Timer1
configuration details.

---

## Hardware

| Component | Notes |
|---|---|
| Arduino Uno or Nano | ATmega328P, 16 MHz |
| A3144 Hall-effect sensor | Digital, open-collector, active low |
| Neodymium magnet | Mounted on rotor arm |
| 74HC595 shift register | Drives 8 LEDs from 3 Arduino pins |
| 8× LEDs + 68 Ω resistors | Any colour, mounted on spinning arm |
| DC motor, 300–2000 RPM | With motor driver to isolate switching noise |
| Tactile button | Image select on D4 |

See `docs/WIRING.md` for full pin assignments and assembly guidance.

---

## Images

Three images are stored in PROGMEM (36 columns × 8 bits each, 108 bytes total):

| Index | Image | Description |
|---|---|---|
| 0 | Smiley face | Circular border with eyes and smile |
| 1 | Right arrow | Horizontal shaft with triangular head |
| 2 | Letter P | Block capital letter |

Press the button (D4) to cycle through images. Customize `image.h` to add your
own: each column is a `uint8_t` bitmask — bit 0 = innermost LED, bit 7 = outermost.

---

## How to Build and Flash

### Dependencies

- Arduino IDE 2.x or Arduino CLI
- Board: **Arduino Uno** or **Arduino Nano**
- No additional libraries required

### Via Arduino CLI

```bash
arduino-cli compile --fqbn arduino:avr:uno firmware/pov_display
arduino-cli upload  --fqbn arduino:avr:uno \
    --port /dev/ttyACM0 firmware/pov_display
```

Or open `firmware/pov_display/pov_display.ino` in the Arduino IDE and click Upload.

### Serial Debug

With `#define POV_DEBUG 1` (default), the sketch prints RPM and stability status
at 115200 baud once per second. Set to 0 before final deployment to recover the
~800 bytes of flash used by `Serial`.

---

## Configuration

All tuning constants are in `config.h`:

| Constant | Default | Effect |
|---|---|---|
| `NUM_COLS` | 36 | Angular columns per revolution (10° each) |
| `RPM_MIN` | 200 | Below this RPM, display blanks |
| `RPM_MAX` | 4000 | Above this RPM, display blanks |
| `AUTO_ADVANCE_REVS` | 0 | Revolutions between auto image advance (0 = off) |
| `HALL_PIN` | D2 | INT0 — must be D2 on Uno/Nano |
| `BTN_PIN` | D4 | Image select button |

Increase `NUM_COLS` for finer angular resolution at the cost of shorter column
exposure time. At 600 RPM with 72 columns each column is only ~1.4 ms wide —
test that the LEDs are bright enough before raising it.

---

## Results

- Column timing accuracy: limited by Timer1's 0.5 µs tick resolution
- Speed tracking: re-synchronizes within one revolution of any RPM change
- ISR execution time: < 3 µs (shift register transfer dominates)
- Flash usage: ~3.5 KB (images + ISR code); well within the Uno's 32 KB

---

## File Map

| File | Purpose |
|---|---|
| `firmware/pov_display/pov_display.ino` | Main sketch — setup, loop, serial debug |
| `firmware/pov_display/config.h` | Pin assignments, speed limits, geometry |
| `firmware/pov_display/image.h` | PROGMEM image bitmaps and retrieval helper |
| `firmware/pov_display/leds.h/.cpp` | 74HC595 shift register driver |
| `firmware/pov_display/pov.h/.cpp` | Hall ISR, Timer1 ISR, column scheduler |
| `docs/ARCHITECTURE.md` | Timing diagrams, ISR interaction, shared variable safety |
| `docs/WIRING.md` | Bill of materials, pin table, assembly tips |
