# Architecture

## Overview

A persistence of vision display renders mid-air images by exploiting the human
eye's ~70 ms image retention. A strip of 8 LEDs spins on a rotor; the firmware
flashes each LED at precisely the right moment so consecutive column flashes
appear to the eye as a stable 2-D image.

The fundamental challenge is **timing accuracy under variable rotor speed**. If
the column period is computed once and never updated, changes in motor speed
cause the image to warp or rotate. This firmware measures every revolution period
from the Hall sensor and reprograms Timer1's compare register at the start of
each revolution, so the display continuously adapts.

---

## Interrupt Architecture

Two hardware interrupts cooperate:

```
                                 Hall pulse (once / revolution)
                                          │
                                          ▼
                                    INT0 ISR
                                  ┌─────────────────────────────────┐
                                  │ 1. t_now = micros()              │
                                  │ 2. period = t_now − t_prev       │
                                  │ 3. Guard: period in [MIN, MAX]?  │
                                  │    No  → timerStop(), blank()    │
                                  │    Yes → continue                │
                                  │ 4. col_period = period / 36      │
                                  │ 5. OCR1A = col_period / 0.5 µs  │
                                  │ 6. TCNT1 = 0  (phase reset)      │
                                  │ 7. g_col = 0                     │
                                  └─────────────────────────────────┘

           ┌────── 36 × per revolution (one per column period) ──────┐
           │                                                          │
           ▼                                                          │
    TIMER1_COMPA ISR                                                  │
  ┌──────────────────────────────┐                                   │
  │ 1. col = g_col               │                                   │
  │ 2. Bounds check (col < 36)   │◄──────────────────────────────────┘
  │ 3. mask = IMAGE[img][col]    │  (Timer1 auto-reloads OCR1A in CTC mode)
  │ 4. LEDs::write(mask)         │
  │ 5. g_col++                   │
  └──────────────────────────────┘
```

The key invariant: **g_col = 0 always coincides with the Hall magnet passing
the sensor**. Every subsequent column fires at equal angular increments,
regardless of absolute speed.

---

## Column Timing

```
360° revolution (example: 600 RPM → period = 100 ms)

 Hall ▼                                          Hall ▼ (next rev)
  │←────────────────── 100 ms ─────────────────────►│
  │
  │   col 0    col 1    col 2   … col 35
  │←2.78ms→│←2.78ms→│←2.78ms→│…│←2.78ms→│
  │  LEDs   │  LEDs   │  LEDs   │  LEDs   │
  │ [0x81]  │ [0x81]  │ [0xBD]  │ [0x00]  │
```

`col_period_us = revolution_period_us / NUM_COLS`

Timer1 runs in CTC mode (Clear Timer on Compare Match). When TCNT1 reaches
OCR1A it resets to 0 and fires COMPA interrupt — no software reload needed.

---

## Speed Adaptation

Each Hall pulse triggers a full recalculation:

1. `period_new = t_hall_now − t_hall_prev`
2. `OCR1A_new = (period_new / NUM_COLS) / T1_TICK_US − 1`
3. `TCNT1 = 0` — phase-locks column 0 to the Hall event

If `period_new` is outside `[PERIOD_US_MIN, PERIOD_US_MAX]` the timer is stopped
and all LEDs blanked. The display resumes automatically once the rotor re-enters
the window.

```
      RPM
4000 ─────────────────────────── upper limit (blanked above)
      ┌────────────────────────┐
      │   stable display zone  │
 200 ─┴────────────────────────┘─ lower limit (blanked below)
      spin-up          spin-down
```

---

## Timer1 Configuration

| Parameter | Value |
|---|---|
| Mode | CTC (WGM12 = 1) |
| Prescaler | 8 (CS11 = 1) |
| Tick period | 0.5 µs |
| OCR1A range | 1 … 65535 |
| Representable col period | 1 µs … 32.8 ms |
| At 200 RPM, 36 cols | col_period = 8.33 ms → OCR1A = 16 665 |
| At 4000 RPM, 36 cols | col_period = 416 µs → OCR1A = 832 |

---

## Shift Register Timing

`LEDs::write()` bit-bangs the 74HC595 using direct port register access
(PORTB) rather than `digitalWrite()`. Each clock toggle is ~62.5 ns at 16 MHz.
Full 8-bit transfer: 16 toggles + 2 latch toggles ≈ **1.1 µs**.

The COMPA ISR total execution time (including the PROGMEM read and shift
register transfer) is under 3 µs — less than 1% of the shortest column period
at 4000 RPM.

---

## PROGMEM Image Storage

Each 36-byte image is stored in flash with `PROGMEM`. Reads use
`pgm_read_byte()` which emits an `LPM` (Load Program Memory) instruction.

```c
uint8_t imageColumn(const uint8_t *pgm_img, uint8_t col) {
    return pgm_read_byte(pgm_img + col);
}
```

Three images × 36 bytes = 108 bytes in flash, 0 bytes in SRAM. The ATmega328P
has 2 048 bytes of SRAM; avoiding PROGMEM for images of this size would still
be safe but is good practice and avoids fragmentation.

---

## Shared Variable Safety

| Variable | Written by | Read by | Protection |
|---|---|---|---|
| `g_hall_prev_us` | INT0 ISR | INT0 ISR | None needed (same ISR) |
| `g_period_us` | INT0 ISR | `loop()` | `cli()/sei()` in `currentRPM()` |
| `g_col` | INT0 ISR (reset) and COMPA ISR (inc) | COMPA ISR | Priority: INT0 > COMPA; both are ISRs, neither preempts on AVR |
| `g_rev_count` | INT0 ISR | `loop()` | `cli()/sei()` in `update()` |
| `g_stable` | INT0 ISR | `loop()` | Single byte — atomic on AVR |
| `g_image_idx` | `loop()` | COMPA ISR | Single byte — atomic on AVR; updated only when stable image boundary ensures no visual glitch |

On the AVR (non-preemptive ISR model), an ISR cannot be preempted by another ISR
unless the global interrupt flag is re-enabled inside it (which this firmware
never does). INT0 and TIMER1_COMPA therefore cannot interleave with each other,
only with `loop()`.

---

## Image Format

Each image is an array of `NUM_COLS` (36) bytes. Each byte is an 8-bit bitmask:

```
Bit 7 = LED 7 (outermost end of strip)
Bit 6 = LED 6
…
Bit 0 = LED 0 (innermost end of strip)

A 1 bit means the LED is ON for that column.
```

Example: a full vertical bar at column 5 → `0xFF`. The top half only → `0xF0`.
