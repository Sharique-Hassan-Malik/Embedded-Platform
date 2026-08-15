# Architecture

## Overview

The synthesizer is a single-file embedded C project targeting the PIC32MX270F256B
microcontroller.  It implements a two-operator FM voice — one modulator oscillator
feeds phase-modulation sidebands into one carrier oscillator — producing a wide
range of timbres from pure sine waves through bell tones, electric piano patches
and metallic noise depending on the operator ratio and modulation index.

Audio is output through an MCP4921 12-bit SPI DAC at 22 050 Hz.  MIDI input
arrives on UART1 at the standard 31 250 baud rate.  All DSP computation runs
without floating-point arithmetic; the soft-FPU is used only once during startup
to populate the lookup tables.

---

## Block Diagram

```
MIDI DIN-5
    │
    ▼
 Optocoupler
    │
    ▼
 UART1 RX (31 250 baud)
    │
    ▼  [UART RX ISR → midi_rx_byte mailbox]
 midi_parse()  ──────────────────────────────────────────────────────────┐
    │                                                                     │
    ├── Note On  ──► fm_voice_note_on()                                  │
    ├── Note Off ──► fm_voice_note_off()                                 │
    └── CC       ──► handle_cc()                                         │
                        │                                                 │
              ┌─────────┴──────────┐                                     │
              │                    │                                      │
         adsr_set_params()   lfo_set_params()                            │
         fm_voice_set_*()                                                 │
                                                                          │
 Timer2 ISR @ 22 050 Hz ◄──────────────────────────────────────────────┘
    │
    ▼
 fm_voice_tick()
    │
    ├── lfo_tick()              ← LFO phase advance + wavetable lookup
    │       └── wavetable_interp()   [mips_dsp.S]
    │
    ├── Modulator path:
    │     mod_phase += mod_inc
    │     mod_sample = wavetable_interp(sine_table, mod_phase)   [mips_dsp.S]
    │     mod_sample = mod_sample * mod_index >> 8
    │
    ├── Carrier path:
    │     carrier_phase += carrier_inc ± vibrato_offset
    │     modulated_phase = carrier_phase + (mod_sample << 7)
    │     carrier_sample = wavetable_interp(sine_table, modulated_phase)  [mips_dsp.S]
    │
    ├── adsr_tick()             ← amplitude envelope
    │
    ├── q15_mul(carrier, env)   [mips_dsp.S]
    ├── q15_mul(output, trem)   (amplitude LFO only)
    └── q15_mul(output, vel)    ← velocity scaling
             │
             ▼
         q15_to_dac()
             │
             ▼
         dac_write()   ──► SPI1 ──► MCP4921 ──► analog audio out
```

---

## Module Descriptions

### `mips_dsp.S` — MIPS32 assembly hot paths

Two functions optimized in MIPS32r2 assembly for the PIC32MX M4K core.

**`wavetable_interp(table, phase)`**

Performs an 8-bit linear-interpolated lookup into a 1 024-point Q15 sine table.

The 32-bit DDS phase accumulator layout:

```
bit 31                                              bit 0
┌────────────┬──────────┬──────────────────────────────┐
│  index     │  frac8   │       sub-fraction            │
│  [31:22]   │  [21:14] │       [13:0]                  │
│  10 bits   │  8 bits  │       14 bits                 │
└────────────┴──────────┴──────────────────────────────┘
```

Interpolation: `y = y0 + ((y1 - y0) * frac8) >> 8`

Estimated cost: 14 cycles (no cache miss).

**`q15_mul(a, b)`**

Signed Q15 multiply returning `(a * b) >> 15`.  Uses the MIPS `mult`
instruction which produces a 64-bit result in `hi:lo`.  The 32-bit
Q15 result is extracted by right-shifting `lo` by 15 and OR-ing in
the appropriate bits from `hi`.

Estimated cost: 6 cycles.

### `wavetable.c` — Table initialization

Builds `sine_table[1024]` and `midi_phase_inc[128]` at startup using
`sin()` and `pow()` from `math.h`.  The soft-FPU is slow (~80 cycles
per `sin()` call) but the total one-time cost is around 1.5 ms, which
is imperceptible.  Alternatively, `tools/gen_tables.py` pre-computes
both tables and writes a `wavetable_gen.c` file that initialises them
at link time, saving the startup computation.

### `adsr.c` — Amplitude envelope

Fixed-point state machine with four stages: Attack, Decay, Sustain and Release.
All amplitudes are Q15 (0 to 32 767).  Stage rates are computed once in
`adsr_set_params()` from millisecond durations:

```
attack_rate  = 32767 / (attack_ms  * SAMPLE_RATE / 1000)
decay_rate   = (32767 - sustain_level) / (decay_ms * SAMPLE_RATE / 1000)
release_rate = sustain_level / (release_ms * SAMPLE_RATE / 1000)
```

`adsr_tick()` is called once per sample from the Timer2 ISR and costs ≈ 12 cycles.

### `lfo.c` — Low-frequency oscillator

Uses the same DDS accumulator mechanism and `wavetable_interp()` as the audio
oscillators.  Rate is configured in centihz (0.01 Hz units) to avoid floating-point
in the UART ISR path.  The LFO output is scaled by `depth` via `q15_mul()` and
then applied either as a pitch offset (vibrato) or an amplitude gain (tremolo)
inside `fm_voice_tick()`.

### `midi.c` — MIDI byte-stream parser

A three-state machine (PARSE_IDLE, PARSE_DATA1, PARSE_DATA2) that reassembles
MIDI messages from the raw byte stream.  Running status is supported so
sequencers omitting repeated status bytes work correctly.  System Exclusive and
real-time messages are discarded.  Only Note On, Note Off and Control Change
messages are acted upon.

### `dac.c` — MCP4921 driver

Configures SPI1 in 16-bit master mode at 10 MHz (PBCLK / 4).  `dac_write()`
asserts /CS, writes the 16-bit MCP4921 command word (channel A, 1× gain,
active output) and deasserts /CS.  The busy-wait on `SPIBUSY` completes in
1.6 µs at 10 MHz, well within the 45.4 µs sample budget.

### `fm_synth.c` — FM voice engine

The core DSP engine.  `fm_voice_tick()` runs the entire signal chain in ≈ 220
cycles:

| Step | Cost (cycles) |
|---|---|
| LFO tick (phase + interp + q15_mul) | 28 |
| Modulator phase advance | 2 |
| Modulator wavetable lookup | 14 |
| Modulator index scaling | 6 |
| Carrier phase advance + vibrato | 8 |
| FM phase modulation (add) | 2 |
| Carrier wavetable lookup | 14 |
| ADSR tick | 12 |
| Envelope multiply (q15_mul) | 6 |
| Velocity multiply (q15_mul) | 6 |
| **Total** | **98** |

The remaining 3 530 cycles in the 22 050 Hz period are available for interrupt
overhead and UART byte processing.

---

## Fixed-Point Representation

All audio values use Q15 format: a signed 16-bit integer where 32 767 represents
+1.0 and -32 768 represents -1.0.

The DDS phase accumulator is a 32-bit unsigned integer.  One full revolution
of the sine table equals a full-scale (2^32) accumulator overflow.  The phase
increment for a given frequency is:

```
phase_inc = round(freq / SAMPLE_RATE * 2^32)
```

For A4 (440 Hz) at 22 050 Hz sample rate:
```
phase_inc = 440 / 22050 * 4 294 967 296 ≈ 85 764 126 (0x51EB851F)
```

Frequency resolution is `SAMPLE_RATE / 2^32 ≈ 0.005 mHz`, far finer than
any audible pitch discrimination.

---

## Modulation Index and Timbre

FM synthesis produces sidebands at frequencies `fc ± n*fm` where `fc` is the
carrier frequency, `fm` is the modulator frequency and `n` is a positive integer.
The amplitude of each sideband is determined by a Bessel function of the
modulation index `I`.

In this implementation `mod_index` (Q8, 0–1023) controls how strongly the
modulator phase offset perturbs the carrier phase:

| `mod_index` | Approximate `I` | Timbre character |
|---|---|---|
| 0 | 0.0 | Pure sine |
| 192 | 0.75 | Soft tone, 2–3 sidebands |
| 384 | 1.5 | Bright, piano-like |
| 512 | 2.0 | Rich harmonic content |
| 768 | 3.0 | Complex, bell-like |
| 1023 | 4.0 | Metallic, noisy |

The operator ratio `op_ratio_q8` sets `fm = fc * op_ratio_q8 / 256`.  Integer
ratios (256, 512, 768…) produce harmonic spectra.  Non-integer ratios produce
inharmonic sidebands that are characteristic of bell and gong timbres.

---

## Hardware Schematic Notes

```
PIC32MX270F256B                    MCP4921
─────────────────                  ────────
RB3  (SCK1)  ───────────────────►  SCK
RB5  (SDO1)  ───────────────────►  SDI
RB0  (/CS)   ───────────────────►  /CS
                                   LDAC  ── GND  (automatic latch)
                                   Vref  ── 3.3 V (or filtered 3.3 V)
                                   VOUT  ── audio output
                                   AVDD  ── 3.3 V
                                   AGND  ── GND

MIDI DIN-5 connector               PC-900 or 6N138 optocoupler
─────────────────                  ─────────────────────────────
Pin 4 ── 220 Ω ──► LED(+)         OUT ── 10 kΩ pull-up to 3.3 V
Pin 5 ──────────►  LED(-)                     │
                                   PIC32 RB13 ─┘  (U1RX)

Activity LED
────────────
PIC32 RA0 ── 330 Ω ── LED(+) ── GND
```

---

## Building

Open MPLAB X IDE, create a new project for PIC32MX270F256B, add all `.c` files
from `firmware/` to the project source list, add `mips_dsp.S` to the assembler
source list and select the XC32 toolchain.  No external libraries are required
beyond the XC32 standard library (math.h is needed only for wavetable_init).

Compiler flags: `-O2 -mips32r2`

---

## File Map

| File | Description |
|---|---|
| `firmware/main.c` | PIC32MX hardware init, Timer2 audio ISR, UART MIDI ISR, CC dispatch |
| `firmware/mips_dsp.S` | MIPS32 assembly: `wavetable_interp` and `q15_mul` |
| `firmware/wavetable.c` | Sine table and MIDI phase increment table initialization |
| `firmware/wavetable.h` | Table declarations and function prototypes |
| `firmware/fm_synth.c` | Two-operator FM voice engine |
| `firmware/fm_synth.h` | `fm_voice_t` struct and API |
| `firmware/adsr.c` | Attack/Decay/Sustain/Release envelope |
| `firmware/adsr.h` | `adsr_t` struct and API |
| `firmware/lfo.c` | Low-frequency oscillator (vibrato and tremolo) |
| `firmware/lfo.h` | `lfo_t` struct and API |
| `firmware/midi.c` | MIDI byte-stream parser with running-status support |
| `firmware/midi.h` | `midi_msg_t` struct and API |
| `firmware/dac.c` | MCP4921 12-bit DAC driver over SPI1 |
| `firmware/dac.h` | DAC API and `q15_to_dac()` conversion |
| `tools/gen_tables.py` | Offline table generator (alternative to runtime init) |
| `tools/midi_test.py` | Python test harness — sends MIDI sequences over serial |
| `docs/ARCHITECTURE.md` | This document |
