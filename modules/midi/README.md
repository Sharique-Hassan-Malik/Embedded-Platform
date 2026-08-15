# DIY MIDI Controller with USB-HID

> Part of the [Embedded Platform](../../README.md) — nineteen firmware projects
> behind one build harness. This folder builds and runs on its own; `embed build
> --only midi` builds it alongside the rest.

A USB MIDI controller firmware for Arduino Leonardo (ATmega32U4) implementing
8 velocity-sensitive FSR pads, 4 rotary encoder knobs and 4 linear faders.
All MIDI protocol logic is written from scratch without using the Arduino MIDI
library. The controller enumerates as a class-compliant USB MIDI device and
works natively in any DAW without drivers on macOS and Linux.

---

## The Hard Part

Most MIDI controller projects reach for the Arduino `MIDI.h` or `Control Surface`
libraries. This project implements the full MIDI protocol layer from scratch:
USB MIDI event packet construction (4-byte packets with Code Index Number field
per the USB MIDI 1.0 class specification), 14-bit pitch bend encoding, velocity
curve math and all message type constants from the MIDI 1.0 specification.

The `MIDIUSB` library is used only as a USB write transport — analogous to
`Serial.write()` for hardware UART MIDI — to send raw bytes over the ATmega32U4's
USB endpoint. All protocol logic lives in `midi_protocol.cpp`.

Additional engineering challenges:

**Velocity detection via peak sampling.** Unlike keyboards that use a dual-switch
timing method, FSR pads measure force directly. A 20 ms attack window tracks the
peak ADC value after threshold crossing; velocity is derived from that peak.
A square-root curve then compresses the top of the velocity range, giving better
resolution for light touches.

**Gray-code quadrature decoding.** Rotary encoders output two signals 90° out of
phase. A 16-entry lookup table indexed by the packed (previous, current) state
pair decodes direction in O(1) without conditionals, correctly handling
mechanical chatter through invalid transition entries returning 0.

**ADC deadband for faders.** A 10-bit ADC reading a static fader still fluctuates
by ±2–3 LSB due to noise. A deadband filter holds the last transmitted CC value
and only sends an update when the ADC moves by more than `FADER_DEADBAND` counts,
keeping the host MIDI bus quiet when no fader is in motion.

---

## Architecture

```
midi_controller.ino
│
├── PadScanner      — 74HC4051 MUX scan, FSR peak detection, NoteOn/Off
├── EncoderBank     — Gray-code quadrature state machine, CC output
├── FaderBank       — ADC with deadband hysteresis, CC output
└── MIDI namespace  — protocol constants, message encoding, USB flush
```

See `docs/ARCHITECTURE.md` for a detailed breakdown of each subsystem including
state-machine diagrams and the USB MIDI event packet format.

---

## Hardware

| Component | Qty |
|---|---|
| Arduino Leonardo (ATmega32U4) | 1 |
| FSR 402 force-sensitive resistor pads | 8 |
| 74HC4051 8-to-1 analog multiplexer | 1 |
| 10 kΩ resistors (FSR pull-down) | 8 |
| EC11 rotary encoders (20 detent/rev) | 4 |
| 100 nF ceramic capacitors (encoder debounce) | 4 |
| 10 kΩ linear potentiometers | 4 |

See `docs/WIRING.md` for pin assignments and schematic descriptions.

---

## CC Map

| Control | CC | Notes |
|---|---|---|
| Knob 1–4 | CC 16–19 | Encoder relative, clamped 0–127 |
| Fader 1–4 | CC 20–23 | Absolute, deadband filtered |
| Pads 1–8 | Note 36–43 | Velocity from FSR peak, sqrt curve |

All controls default to MIDI channel 1. Change `GLOBAL_CHANNEL` in `config.h`.

---

## How to Build and Flash

### Dependencies

- Arduino IDE 2.x or Arduino CLI
- Board: **Arduino Leonardo** (select in Tools → Board)
- Library: **MIDIUSB** (install via Library Manager)

### Steps

```bash
# Via Arduino CLI
arduino-cli lib install MIDIUSB
arduino-cli compile --fqbn arduino:avr:leonardo firmware/midi_controller
arduino-cli upload  --fqbn arduino:avr:leonardo \
    --port /dev/ttyACM0 firmware/midi_controller
```

Or open `firmware/midi_controller/midi_controller.ino` in the Arduino IDE,
select **Arduino Leonardo** as the board and click Upload.

### Verify

On macOS: open **Audio MIDI Setup → MIDI Studio** — the Leonardo should appear.
On Linux: run `aconnect -l` — the device appears as `Arduino Leonardo`.
On Windows: install the Arduino USB MIDI driver from arduino.cc.

---

## File Map

| File | Purpose |
|---|---|
| `firmware/midi_controller/midi_controller.ino` | Main sketch — setup and loop |
| `firmware/midi_controller/config.h` | Pin assignments, thresholds and CC numbers |
| `firmware/midi_controller/midi_protocol.h/.cpp` | MIDI message encoding from scratch |
| `firmware/midi_controller/pads.h/.cpp` | FSR pad scanning with velocity detection |
| `firmware/midi_controller/encoders.h/.cpp` | Quadrature encoder state machine |
| `firmware/midi_controller/faders.h/.cpp` | Fader ADC with deadband filter |
| `docs/ARCHITECTURE.md` | Subsystem diagrams and protocol details |
| `docs/WIRING.md` | Bill of materials and wiring guide |

---

## Customization

- **MIDI channel:** set `GLOBAL_CHANNEL` in `config.h` (0 = Ch 1).
- **Note layout:** change `PAD_NOTE_BASE` to shift the pad octave.
- **CC assignments:** adjust `ENCODER_CC_BASE` and `FADER_CC_BASE`.
- **Velocity sensitivity:** raise `PAD_THRESH_ON` if pads trigger too easily;
  lower it if light touches are missed.
- **Fewer pads:** set `PAD_COUNT` to 4 and wire FSRs directly to A0–A3,
  removing the MUX and its select-pin setup in `pads.cpp`.

---

## Results

- Latency from pad strike to MIDI NoteOn: < 25 ms (20 ms attack window + USB poll interval)
- CC resolution: 7-bit (128 steps) for both encoders and faders
- Fader noise floor: suppressed below 3 ADC counts by deadband filter
- Host compatibility: macOS (class-compliant, zero config), Linux (ALSA), Windows (driver required)
