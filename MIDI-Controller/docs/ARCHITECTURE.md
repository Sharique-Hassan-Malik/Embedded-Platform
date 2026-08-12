# Architecture

## Overview

The firmware is split into four independent subsystems plus a MIDI protocol
layer. Each subsystem owns its own state and exposes only `begin()` and
`update()`. The main sketch calls all three `update()` functions each loop
iteration and flushes the USB MIDI buffer at the end.

```
loop()
  ├── PadScanner::update()    → MIDI::sendNoteOn / sendNoteOff
  ├── EncoderBank::update()   → MIDI::sendControlChange
  ├── FaderBank::update()     → MIDI::sendControlChange
  └── MIDI::flush()
```

---

## USB MIDI Layering

USB MIDI on the ATmega32U4 is a two-layer stack:

| Layer | What it does | Who implements it |
|---|---|---|
| USB transport | Enumerates as USB Audio class device, manages endpoints and interrupt transfers | Arduino 32U4 USB core + MIDIUSB library |
| MIDI protocol | Message type constants, status byte encoding, 14-bit pitch bend packing, velocity curves, USB MIDI event packet construction (CIN field) | `midi_protocol.h/.cpp` — from scratch |

The MIDIUSB library is used only for two calls: `MidiUSB.sendMIDI(packet)` and
`MidiUSB.flush()`. It is a thin USB write wrapper — equivalent to `Serial.write()`
for hardware UART MIDI. All MIDI message logic lives in `midi_protocol.cpp`.

---

## USB MIDI Event Packet Format

Each message sent to the host is a 4-byte packet (USB MIDI 1.0 spec, Table 4-1):

```
Byte 0: [cable_number (7:4)] [CIN (3:0)]
Byte 1: MIDI status byte
Byte 2: first data byte
Byte 3: second data byte (0 for single-data-byte messages)
```

The Code Index Number (CIN) tells the host driver the message type and how many
data bytes are valid, independent of the status byte itself.

---

## Pad Subsystem

### Hardware

Eight FSR (force-sensitive resistor) pads are read through a 74HC4051 8-to-1
analog multiplexer. Three address lines (S0, S1, S2) select one of eight FSR
channels; the common output is connected to a single ADC pin (A0). This reduces
the required pin count from 8 analog pins to 1 analog + 3 digital.

Electrical model: FSR forms the lower leg of a voltage divider with a fixed
pull-down resistor. Greater force → lower FSR resistance → higher voltage at COM.

### Velocity Detection

Velocity is measured as the peak ADC value during an attack window of
`PAD_PEAK_MS` milliseconds after the pressure first exceeds `PAD_THRESH_ON`.

```
pressure
  │
1023│         ┌──peak────┐
    │        /│           \
    │       / │            \
THRESH_ON  /  │             \
    │─────/   │              \──────────
    │    /    │               \
    └───/─────┼────────────────\───── time
         arm  │   PEAK_MS      release
              └──NoteOn sent here
```

The state machine has three states:

```
IDLE ──(adc ≥ THRESH_ON)──► DETECTING ──(PEAK_MS elapsed)──► ACTIVE
  ▲                                                              │
  └────────────────────(adc < THRESH_OFF)───────────────────────┘
```

DETECTING to ACTIVE is a timed transition; ACTIVE to IDLE is level-triggered
with hysteresis (`THRESH_OFF < THRESH_ON`) to prevent chatter.

### Velocity Curve

The raw linear mapping is passed through a square-root curve before being sent:

```
v_out = sqrt(v_in / 127) × 127
```

This gives finer resolution at low velocities (light touches) where musical
expression is most sensitive, while compressing the top of the range.

---

## Encoder Subsystem

Rotary encoders output quadrature (Gray code): two signals A and B, each
90° out of phase. The direction of rotation is determined by which signal
leads the other.

### State Machine

The decoder uses a 16-entry lookup table indexed by `(prev_state << 2) | curr_state`,
where `state = (A << 1) | B`. Each table entry is -1, 0 or +1:

```
        curr: 00  01  11  10
  prev  00:    0  -1   0  +1
        01:   +1   0  -1   0
        11:    0  +1   0  -1
        10:   -1   0  +1   0
```

A +1 entry is one clockwise detent; -1 is one counter-clockwise detent.
Invalid transitions (diagonal jumps, which indicate a missed state) produce 0.

Each detent increments or decrements the encoder's stored CC value by 1,
clamped to 0–127, and a CC message is sent immediately.

---

## Fader Subsystem

Linear potentiometers are read directly on dedicated ADC pins. A 10-bit ADC
reading (0–1023) is converted to a 7-bit CC value (0–127) by right-shifting
3 bits (integer divide by 8).

A deadband of `FADER_DEADBAND` ADC counts suppresses noise: a CC is sent only
when the reading changes by more than the deadband from the last transmitted
value. This prevents the ADC noise floor from generating continuous CC traffic
when the fader is untouched.

---

## CC Map

| Control | CC Number | Range |
|---|---|---|
| Knob 1 | 16 | 0–127 |
| Knob 2 | 17 | 0–127 |
| Knob 3 | 18 | 0–127 |
| Knob 4 | 19 | 0–127 |
| Fader 1 | 20 | 0–127 |
| Fader 2 | 21 | 0–127 |
| Fader 3 | 22 | 0–127 |
| Fader 4 | 23 | 0–127 |

Pad notes start at C2 (MIDI note 36) and ascend chromatically: C2, C#2, D2 … G2.
All controls transmit on the channel configured in `GLOBAL_CHANNEL` (default: 1).
