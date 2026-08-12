# Architecture

## Overview

A charge-time measurement (CTM) capacitive sensing library for PIC16
microcontrollers, built without any dedicated touch-sensing hardware.  The
library uses only a GPIO pin, a resistor and Timer1.  A gesture layer converts
raw channel activity into tap, hold and swipe events.  The demo application
streams live telemetry and gesture events over UART at 9 600 baud.

---

## Measurement Principle

### Charge-time measurement

Every capacitive touch electrode has a stray capacitance `C₀` when nothing
is touching it.  A finger adds a parallel capacitance `Cf` (typically
10–50 pF for a 2 cm² pad).  The library measures the time for the electrode
to discharge through a known resistor and uses that time as a proxy for
total capacitance:

```
τ = R × C    →    tₛₜₒₚ = R × (C₀ + Cf) × ln(Vdd / Vil)
```

where `Vil` is the GPIO low-level input threshold (~0.15 × Vdd for PIC16).
Substituting `ln(Vdd/Vil) = ln(1/0.15) ≈ 1.897`:

```
tₛₜₒₚ ≈ 1.897 × R × C
```

With R = 1 MΩ and C₀ = 20 pF:
```
tₛₜₒₚ ≈ 1.897 × 10⁶ × 20×10⁻¹² = 37.9 µs
```

At 16 MHz (Timer1 ticks at Fosc/4 = 4 MHz, period 250 ns):
```
count_no_touch ≈ 37.9 µs / 0.25 µs ≈ 152 counts
```

A 20 pF finger adds:
```
Δcount ≈ 1.897 × 10⁶ × 20×10⁻¹² / 0.25×10⁻⁶ ≈ 152 counts
```

In practice deltas of 50–300 counts are typical depending on pad size and
finger contact area.  The default threshold of 80 counts is conservative;
see Tuning Methodology below.

### Measurement cycle (per channel, per sample)

```
1. Drive pin LOW  (output) ──────────── discharge C through GPIO driver
   Wait ~4 µs (16 NOPs at 16 MHz)
2. Drive pin HIGH (output) ──────────── charge C through GPIO driver
   Wait ~2 µs ( 8 NOPs)
3. Switch pin to input (hi-Z) ────────── C now discharges through R to GND
   Reset and start Timer1
4. Poll pin ──────────────────────────── until pin reads LOW
   Stop Timer1
5. Return 16-bit count
```

Steps 3–4 run with interrupts disabled (saved/restored around the critical
section) to eliminate Timer1 jitter from ISR latency.  Each measurement
takes approximately 40–200 µs depending on C.  Four oversampled measurements
per channel at 30 Hz cost roughly 4 × 4 channels × 100 µs = 1.6 ms per
scan cycle.

---

## Module Descriptions

### `touch_hw.c/.h` — Pin map

The single user-edited file.  `TOUCH_PINS[]` maps each logical channel index
to the physical PORT, LAT, TRIS, ANSEL registers and single-bit mask.  Adding
a channel means appending one struct literal and incrementing `TOUCH_CH_COUNT`.

### `touch.c/.h` — CTM engine

**Oversampling.** `measure_oversampled(ch)` calls `measure_channel(ch)`
`TOUCH_OVERSAMPLE` times and returns the integer average.  Four samples
reduce Timer1 quantisation noise by a factor of two (√4) without measurable
latency impact at 30 Hz.

**IIR baseline tracker.** The baseline follows slow environmental drift
(humidity, temperature, proximity of conductive surfaces) via:

```
baseline += (raw − baseline) >> TOUCH_BASELINE_SHIFT
```

With `TOUCH_BASELINE_SHIFT = 6` (divide-by-64), the time constant is
64 scan ticks.  At 30 Hz this is approximately 2.1 seconds — slow enough
to ignore a tap but fast enough to adapt to a user resting their hand near
the panel.  The baseline is updated only when the channel is inactive to
prevent a long hold from training the baseline upward and erasing the delta.

**Hysteresis.** Touch activates when `delta > threshold` and releases when
`delta < threshold − TOUCH_HYST` (default 20 counts).  This prevents
chatter when the finger is at the edge of the detection zone.

### `gestures.c/.h` — Gesture recogniser

**Per-channel state machine:**

```
IDLE ──active──► DOWN
                  │
            timer < TAP_MAX ──release──► enqueue(TAP)  → IDLE
                  │
            timer >= HOLD_MIN ─────────► enqueue(HOLD) → HOLD_FIRED
                                                          │
                                                     release → IDLE
```

**Swipe detection** runs in parallel.  When any channel transitions from
IDLE to DOWN, the library records the channel index and resets a swipe
inter-channel timer.  When the next channel activates within `SWIPE_MAX_TICKS`
and is adjacent (index differs by exactly 1), a SWIPE_LEFT or SWIPE_RIGHT
event is emitted.  The direction is defined by which index is smaller:

```
prev_ch < new_ch  →  SWIPE_RIGHT  (finger moved toward higher-index pads)
prev_ch > new_ch  →  SWIPE_LEFT
```

**Event queue.** Up to 4 events are buffered in a circular queue.  The caller
drains it with `gestures_get()` which returns `GESTURE_NONE` when empty.
If the queue overflows, the oldest event is discarded silently.

### `uart.c/.h` — Polled EUSART driver

SPBRG = 415 for 9 600 baud at 16 MHz (error < 0.1%).  Used only by the demo
application; the library itself has no UART dependency.

### `main.c` — Demo application

Timer0 in 8-bit mode with 1:256 prescaler generates ~61 Hz overflows.  A
software divide-by-two produces the ~30 Hz scan tick.  Each tick:

1. `touch_scan()` — measure all channels, update baseline and active flags
2. `gestures_update()` — advance gesture state machines
3. Print delta telemetry line: `D: +NNN +NNN +NNN +NNN  [T0T1T2T3]`
4. Drain gesture queue and print any events

---

## Tuning Methodology

### Step 1 — Select series resistor

The series resistor `R` (to GND) sets the measurement time scale.

| R | C₀ = 20 pF | Cf = 20 pF extra | Δcount (16 MHz) |
|---|---|---|---|
| 470 kΩ | ≈ 71 | ≈ 71 | ≈ 71 |
| 1 MΩ | ≈ 152 | ≈ 152 | ≈ 152 |
| 2.2 MΩ | ≈ 334 | ≈ 334 | ≈ 334 |

Smaller pads (lower C₀ and lower Cf) benefit from larger R.  Larger pads
can use smaller R to shorten measurement time.

**Recommended starting point:** 1 MΩ for a 2–4 cm² pad.

### Step 2 — Measure baseline and noise floor

Power up the board and run the demo with no finger present.  The telemetry
will show the raw delta bouncing in a small window, typically ±5–15 counts.
Note the peak-to-peak noise floor: `N_pp`.

### Step 3 — Measure touch delta

Touch the pad firmly and hold for 2 seconds.  Read the stable delta from
the telemetry.  Note the minimum consistent delta across several touches: `Δmin`.

### Step 4 — Set threshold

A good threshold places the detection point midway between the noise floor
and the minimum touch delta:

```
threshold = (N_pp / 2) + Δmin / 2
```

Round up to the nearest 10 for margin.  Set via `touch_set_threshold(ch, thr)`
after `touch_init()` and before `touch_calibrate()`.

### Step 5 — Adjust hysteresis

If the active flag chatters on the rising or falling edge, increase
`TOUCH_HYST` in `touch.h`.  Values of 15–30 are typical.

### Step 6 — Tune baseline adaptation rate

If the baseline drifts toward the touch level during a long hold, reduce
`TOUCH_BASELINE_SHIFT` to slow adaptation (e.g. 7 or 8).  If the baseline
does not recover quickly enough after the environment changes (e.g. device
moved to a humid room), increase it (e.g. 5).

### Step 7 — Electrode geometry notes

| Factor | Effect |
|---|---|
| Larger pad area | More Cf per finger contact — higher Δcount |
| Ground plane below pad | Reduces stray C₀ noise — better SNR |
| Guard ring (driven by same signal phase) | Shields pad from lateral coupling |
| Thick PCB overlay (>1 mm) | Attenuates Cf — requires lower R or larger pad |
| Multiple fingers | Multiple channels activate; gestures layer handles this correctly |

### Step 8 — Swipe and tap timing

At 30 Hz:
- `TAP_MAX_TICKS` = 15 → 500 ms.  Reduce to 8 (267 ms) for more responsive UI.
- `SWIPE_MAX_TICKS` = 8 → 267 ms.  Increase to 12 for a more forgiving swipe window.
- `HOLD_MIN_TICKS` = 30 → 1 000 ms.  Reduce to 20 (667 ms) for a faster hold trigger.

---

## Hardware Schematic

```
PIC16F1829
──────────
RA0 ──── 1 MΩ ──── GND       CH0 electrode pad
RA1 ──── 1 MΩ ──── GND       CH1 electrode pad
RA2 ──── 1 MΩ ──── GND       CH2 electrode pad
RC0 ──── 1 MΩ ──── GND       CH3 electrode pad

RC4 ──────────────────────► UART RX on host (9 600 baud)

VDD  100 nF decoupling cap to GND on each VDD pin
```

Electrode pads can be copper polygons on a PCB, aluminium foil patches, or
conductive fabric.  Any conductor within 5–10 mm of the PCB surface will
couple capacitively.

---

## File Map

| File | Description |
|---|---|
| `firmware/touch_hw.h` | Pin descriptor struct and channel count |
| `firmware/touch_hw.c` | Concrete pin assignments for PIC16F1829 demo board |
| `firmware/touch.h` | CTM engine API: constants, `touch_ch_t`, public functions |
| `firmware/touch.c` | Timer1 measurement, IIR baseline, hysteresis, oversampling |
| `firmware/gestures.h` | Gesture type enum, `gesture_event_t`, timing constants |
| `firmware/gestures.c` | Per-channel state machine and swipe cross-channel detector |
| `firmware/uart.h` | Minimal polled UART API |
| `firmware/uart.c` | EUSART1 at 9 600 baud for demo telemetry |
| `firmware/main.c` | Demo: 30 Hz scan loop, telemetry output, gesture printing |
| `docs/ARCHITECTURE.md` | This document |
