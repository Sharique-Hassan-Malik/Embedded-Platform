# PIC16 CapTouch

> Part of the [Embedded Platform](../../README.md) — nineteen firmware projects
> behind one build harness. This folder builds and runs on its own; `embed build
> --only captouch` builds it alongside the rest.

A charge-time measurement (CTM) capacitive touch library for PIC16
microcontrollers.  No dedicated touch hardware is required: sensing is done
with a plain GPIO pin, a single 1 MΩ resistor and Timer1.  A gesture layer
converts raw channel activity into tap, hold and swipe events through a
clean, interrupt-safe API.  The library ships with a demo application that
streams live delta values and gesture events over UART and documents a
step-by-step threshold tuning procedure for different electrode geometries.

---

## What it does

The library scans up to `TOUCH_CH_COUNT` (default 4) electrodes per call to
`touch_scan()`.  Each channel tracks its own IIR baseline to handle slow
environmental drift in temperature and humidity, and applies hysteresis on
the detection boundary to prevent chatter.  The gesture layer identifies
three interaction types:

- **Tap** — a channel activates and releases within a configurable window
- **Hold** — a channel stays active past a configurable threshold
- **Swipe left / right** — two adjacent channels activate in sequence within
  a configurable inter-channel time window

All timing constants are in scan ticks and can be adjusted without
rewriting any logic.

---

## The hard part

**Sensitivity without dedicated hardware.**  PIC16 devices have no on-chip
capacitive sensing peripheral.  The library exploits the fact that the
discharge time of a RC circuit is proportional to C.  By timing how long a
pin takes to go low through a series resistor — using a 16-bit hardware timer
with 250 ns resolution at 16 MHz — the firmware can detect the ≈10–50 pF
capacitance change a finger adds to a pad.  At 16 MHz with R = 1 MΩ and
C₀ = 20 pF, a no-touch count is roughly 150 Timer1 ticks; a light finger
touch adds 50–150 more.  The gap is large enough to detect reliably even
with the PIC's relatively coarse input threshold.

**Baseline drift without false triggering.**  The baseline IIR filter adapts
only while the channel is inactive, preventing a sustained hold from training
the baseline upward and erasing the delta.  A single guard condition also
allows the baseline to track environmental drops while a hold is in progress,
so the library recovers correctly when conditions change during a long press.

**Swipe detection across independent channels.**  Each channel is stateless
with respect to the others; swipe is detected by recording when a channel
first activates and checking whether the next activation is on an adjacent
channel within a short time window.  No shared state or cross-channel polling
is needed during the normal scan path.

---

## Architecture

See `docs/ARCHITECTURE.md` for the full measurement derivation, module
descriptions, state machine diagrams and a step-by-step tuning methodology
with worked examples for different resistor values and electrode sizes.

---

## Hardware

| Component | Value | Notes |
|---|---|---|
| Microcontroller | PIC16F1829 | 16 MHz internal oscillator |
| Series resistors | 1 MΩ × 4 | One per electrode to GND |
| Electrode pads | 2–4 cm² copper | PCB polygon, foil or conductive fabric |
| Decoupling | 100 nF ceramic | Per VDD pin |

**Pin assignments (demo board)**

| Pin | Function |
|---|---|
| RA0 | Touch channel 0 (left) |
| RA1 | Touch channel 1 |
| RA2 | Touch channel 2 |
| RC0 | Touch channel 3 (right) |
| RC4 | UART TX, 9 600 baud (diagnostic output) |

---

## API reference

```c
/* One-time setup */
void touch_init(void);
void touch_calibrate(void);           /* call with electrodes clear */
void touch_set_threshold(uint8_t ch, uint16_t thr);

/* Per-frame scan (call at your desired rate, typically 10–100 Hz) */
void touch_scan(void);

/* Query current state */
int16_t touch_delta(uint8_t ch);      /* raw − baseline for channel ch */
uint8_t touch_active(uint8_t ch);     /* 1 if currently touched */

/* Gesture layer */
void gestures_init(void);
void gestures_update(void);           /* call after touch_scan() */
gesture_event_t gestures_get(void);   /* dequeue one event; type = GESTURE_NONE if empty */
```

---

## Porting to a different PIC16 or PIC18

1. Open `firmware/touch_hw.h` and update `TOUCH_CH_COUNT`.
2. Open `firmware/touch_hw.c` and edit the `TOUCH_PINS[]` table — one entry
   per channel with the correct PORT, LAT, TRIS and ANSEL register addresses
   and single-bit mask.
3. If the target runs at a different `Fosc`, recalculate the UART SPBRG in
   `uart.c` and verify that Timer1 still offers sufficient resolution for the
   expected charge-time range.
4. Rebuild.  No other files need editing.

---

## Tuning quick-start

1. Program the demo, connect RC4 to a 9 600 baud terminal.
2. Power up with no touch — observe baseline printout.
3. Watch the `D:` telemetry line.  Note the idle noise floor (peak-to-peak
   delta with no touch, typically ±5–15 counts).
4. Touch each pad firmly.  Note the stable delta (typically +80–300 counts).
5. Set `threshold = noise_pp + Δmin / 2`.  Call `touch_set_threshold(ch, thr)`.
6. Power-cycle; verify clean activation with no false triggers.
7. If chatter is seen on activation, increase `TOUCH_HYST` in `touch.h`.

Full tuning derivation and electrode geometry notes are in
`docs/ARCHITECTURE.md`.

---

## Building

1. Open MPLAB X and create a project for PIC16F1829.
2. Select XC8 as the toolchain.
3. Add all `.c` files in `firmware/` to Source Files.
4. Add all `.h` files in `firmware/` to Header Files.
5. Build and program with PICkit 3/4 or SNAP.

---

## Results

- Scan rate: 30 Hz (8 oversampled measurements per channel per scan, 4 channels)
- Measurement time per channel: ~100 µs average (depends on C)
- Total scan time: ≈ 1.6 ms per 30 Hz frame (5% of frame budget)
- Detection threshold: as low as 30 counts above noise at 16 MHz with 1 MΩ
- Typical SNR: > 6:1 on a 3 cm² pad with 1 MΩ and 16 MHz clock
- Gesture latency: at most 1 scan tick (< 34 ms) after event criterion is met
- Flash footprint: ≈ 2 KB (library + gestures + UART demo)
- RAM footprint: 10 bytes per channel (touch_ch_t × 4) + gesture state (~20 bytes)
