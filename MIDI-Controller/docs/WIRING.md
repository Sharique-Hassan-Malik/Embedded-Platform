# Wiring Guide

## Bill of Materials

| Qty | Component | Notes |
|---|---|---|
| 1 | Arduino Leonardo | ATmega32U4 — native USB MIDI |
| 8 | FSR 402 (or similar) | Force-sensitive resistor pads |
| 8 | 10 kΩ resistor | Pull-down for FSR voltage divider |
| 1 | 74HC4051 | 8-to-1 analog multiplexer |
| 4 | Rotary encoder (EC11 or similar) | Mechanical, 20 detent/rev |
| 4 | 100 nF ceramic capacitor | Encoder debounce, one per encoder |
| 4 | 10 kΩ potentiometer (linear) | 60 mm slide fader or rotary |
| 1 | USB Type-B cable | Leonardo ↔ PC |
| — | Perfboard or custom PCB | |

---

## 74HC4051 Multiplexer (Pads)

The MUX connects 8 FSR pads to a single ADC pin.

```
74HC4051 pin    →  Leonardo pin
─────────────────────────────────
 Z  (COM I/O)   →  A0
 S0 (address 0) →  D4
 S1 (address 1) →  D11
 S2 (address 2) →  D12
 E  (enable)    →  GND  (active low, always enabled)
 VCC            →  5 V
 GND            →  GND
 VEE            →  GND
```

### FSR Voltage Divider (repeat ×8)

Each FSR pad forms the upper leg of a voltage divider:

```
5 V ──[FSR]──┬──[10 kΩ]── GND
             │
           Y0…Y7 of 74HC4051
```

Higher pressure → lower FSR resistance → higher voltage → higher ADC reading.
Lightly press a pad and verify ADC rises above `PAD_THRESH_ON` (default 80).
Adjust `PAD_THRESH_ON` in `config.h` if your FSR characteristics differ.

---

## Rotary Encoders

Each encoder has two output pins (A and B) and a common (GND).
Connect A and B through 100 nF capacitors to GND to filter mechanical bounce.

```
Encoder 1  A → D2,  B → D3
Encoder 2  A → D5,  B → D6
Encoder 3  A → D7,  B → D8
Encoder 4  A → D9,  B → D10
```

The firmware enables internal pull-ups on all encoder pins, so no external
pull-up resistors are needed. The capacitors alone are sufficient for debounce
at the polling rates used.

---

## Faders

Connect each potentiometer as a voltage divider:

```
5 V ──[pot wiper track end]
GND ──[pot wiper track other end]
     [pot wiper] → A1 / A2 / A3 / A4
```

| Fader | Pin |
|---|---|
| Fader 1 | A1 |
| Fader 2 | A2 |
| Fader 3 | A3 |
| Fader 4 | A4 |

---

## LED Feedback

The built-in LED on D13 lights while pad 0 is held. To add individual LEDs for
all 8 pads, connect a shift register (74HC595) and update `LED_PINS` in
`config.h` with the appropriate output pins.

---

## DAW Setup

No driver installation is required on macOS or Linux. On Windows, install the
official Arduino USB MIDI driver or use the LoopMIDI + loopback approach.

The device appears as **Arduino Leonardo** in your DAW's MIDI input list.
Select it as a MIDI input source; all pads, knobs and faders should respond
immediately with no additional configuration.
