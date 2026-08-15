# Architecture

## System Overview

```
Device Under Test (DUT)
  │  power rail through 0.1 Ω shunt
  ▼
INA219 current-sense amplifier (I2C, 12-bit, ±40 mV)
  │
Arduino (profiler)
  ├── Timer1 ISR @ 1000 S/s → ring buffer → serial @ 1 Mbaud
  └── ANN_PINS D2–D5 ← annotation GPIO from DUT

USB serial
  │
Python host
  ├── transport.py  — binary packet parser (state machine)
  ├── capture.py    — ring buffer + per-section statistics
  └── plot.py       — live Matplotlib dashboard
```

---

## Firmware Sampling Pipeline

```
Timer1 CTC ISR (fires every 1 ms at 1000 S/s)
  │
  ├── INA219: read shunt voltage register (I2C, ~150 µs)
  │   current_0p1mA = shunt_uV × 10 / shunt_mohm
  │
  ├── read annotation pins → 4-bit mask
  │
  └── push Sample{current, ann_mask} → RingBuffer (lock-free, power-of-2 mask)

Main loop (runs between ISR invocations)
  │
  ├── handle host commands (START, STOP, IDENT)
  │
  └── drain RingBuffer → serial (up to 32 samples per iteration)
        5-byte packet: 'S' | lo | hi | ann | checksum
```

### Timer1 Configuration

| Parameter | Value |
|---|---|
| Mode | CTC (WGM12) |
| Prescaler | 8 (CS11) |
| Tick period | 0.5 µs |
| OCR1A | F_CPU / (8 × rate) − 1 = 1999 at 1000 S/s |
| ISR period | exactly 1 ms |

### INA219 Timing Budget

At 400 kHz I2C, one 16-bit register read takes approximately 55 µs (start +
address + register select + repeated start + address + 2 data bytes + stop).
The ISR is non-preemptible on AVR; the 55 µs I2C read reduces the available
CPU time for other ISRs by 5.5% at 1000 S/s. This is acceptable and within
the Timer1 period.

---

## Wire Packet Format

Each sample is transmitted as a 5-byte binary packet:

```
Byte 0: 0x53 ('S')  — packet marker
Byte 1: current LSB — units of 0.1 mA (unsigned)
Byte 2: current MSB
Byte 3: ann_mask    — bits 0–3 = annotation pin states at sample time
Byte 4: checksum    — 0xFF ^ byte1 ^ byte2 ^ byte3
```

Range: 0–65535 × 0.1 mA = 0–6553.5 mA. Negative currents (reverse flow)
are clamped to 0 for the wire format.

Special single-byte messages:
- `'O'` — overflow: the ring buffer filled before the main loop could drain it
- `'R' lo hi` — sample rate response to IDENT query

### Baud Rate Selection

At 1000 S/s each sample is 5 bytes → 5000 bytes/s minimum throughput.
At 115200 baud the theoretical maximum is ~11500 bytes/s — barely adequate
with protocol overhead. The firmware uses 1 Mbaud to give a ×87 headroom,
leaving the serial link as a non-bottleneck even at higher sample rates.

---

## Host Parser State Machine

```
HUNT state: waiting for 0x53 ('S')
  │
  ├── 0x53 received → PAYLOAD state, reset payload buffer
  ├── 'O' received → fire overflow callback
  ├── 'R' + 2 bytes → parse sample rate (IDENT response)
  └── anything else → stay in HUNT (re-sync)

PAYLOAD state: accumulate 4 bytes
  │
  └── 4 bytes received → verify checksum
        valid   → construct Sample, fire callbacks
        invalid → back to HUNT (re-sync without dropping next packet)
```

Checksum failure recovery is per-packet. A single corrupted byte causes at
most one sample to be lost and the parser immediately re-hunts for the next
'S' marker, preserving all subsequent valid packets.

---

## Annotation Protocol

The DUT drives GPIO pins HIGH to mark the start of a code section and LOW
to mark the end. These pins connect to the profiler's `ANN_PINS` (D2–D5).

```
DUT code:
  digitalWrite(ANN_PIN, HIGH);   // open section
  // ... code being profiled ...
  digitalWrite(ANN_PIN, LOW);    // close section

Profiler ISR: records ann_mask bit at each sample
Host:         detects rising/falling edges in ann_mask stream
              computes statistics over samples within each section
```

Up to 4 independent sections can be open simultaneously (one per channel).
Nested sections on the same channel overwrite the open-section start time.

---

## Statistics Computed per Section

| Metric | Formula |
|---|---|
| Mean (mA) | arithmetic mean of samples in section |
| Peak (mA) | max sample value |
| RMS (mA) | √(mean of squares) |
| Energy (µJ) | mean_mA × supply_mV × duration_s |

Energy uses average current (not RMS) because power = V × I and the supply
voltage is assumed constant. The supply voltage is configurable via `--supply`.

---

## Ring Buffer Safety

The ring buffer uses a lock-free single-producer / single-consumer design:

- Producer (ISR): writes `_buf[_head]` then increments `_head`
- Consumer (loop): reads `_tail` and `_head` with `cli()/sei()` for the
  16-bit index comparison, then reads `_buf[_tail]` and increments `_tail`

On AVR, 16-bit reads are not atomic (two 8-bit LDS instructions). The
consumer disables interrupts only for the index comparison, not for the
data read — safe because the consumer is the only reader of `_tail` and
the producer only advances `_head` after writing data.

Buffer size is a power of 2 so wrap uses a bitmask (`& (SIZE-1)`) rather
than a branch, keeping the ISR body deterministic.
