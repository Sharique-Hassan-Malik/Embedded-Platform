# pic32-fm-synth

A standalone two-operator FM synthesizer voice for the PIC32MX270F256B
microcontroller.  MIDI notes arrive on UART1 at 31 250 baud and audio exits
through an MCP4921 12-bit SPI DAC at a 22 050 Hz sample rate.  No audio
library is used anywhere: the signal chain is pure fixed-point DSP math in C
with the two innermost routines — the interpolated wavetable lookup and the Q15
multiply — hand-coded in MIPS32r2 assembly.

---

## What it does

Plays a single synthesized voice in response to standard MIDI Note On/Off and
Control Change messages.  The voice uses frequency modulation (FM) synthesis:
one oscillator (the modulator) perturbs the instantaneous phase of a second
oscillator (the carrier), generating sidebands whose number and amplitude are
controlled by the modulation index.  The result ranges from a clean sine wave
at index zero through piano-like and bell-like timbres at moderate index values
to metallic, inharmonic textures at high index values.  An ADSR amplitude
envelope and an LFO (selectable between vibrato and tremolo) are layered on top.

---

## The hard part

FM synthesis at audio rate on a 32-bit MIPS microcontroller without a hardware
FPU requires careful fixed-point design throughout.

The sine table lookup uses a 32-bit direct digital synthesis (DDS) accumulator
where the ten most-significant bits index a 1 024-point Q15 table and the next
eight bits drive 8-bit linear interpolation between adjacent entries.  This
gives frequency resolution of `22 050 / 2^32 ≈ 0.005 mHz` — far finer than
audible pitch discrimination — while the interpolation suppresses the harmonic
distortion from table quantization to below -60 dBc.

The Q15 multiply extracts bits [46:15] of the 64-bit MIPS `mult` result by
OR-ing a right-shifted `lo` with a left-shifted `hi`.  This avoids the 32-bit
overflow that would occur if the product were simply truncated.

The Timer2 ISR must complete within one sample period (45.4 µs at 22 050 Hz,
or 3 628 cycles at 80 MHz).  The measured worst-case cost of the full voice
computation plus SPI DAC write is around 380 cycles, leaving 3 248 cycles of
headroom for interrupt latency and UART byte processing.

---

## Architecture

See `docs/ARCHITECTURE.md` for the full block diagram, fixed-point math
derivations, modulation index/timbre table and hardware schematic notes.

---

## Hardware

| Component | Part | Notes |
|---|---|---|
| Microcontroller | PIC32MX270F256B | 28-pin, 80 MHz, 256 KB flash, 64 KB RAM |
| DAC | MCP4921 | 12-bit, SPI, up to 20 MHz clock |
| MIDI optocoupler | PC-900 or 6N138 | DIN-5 to 3.3 V logic |
| Decoupling | 100 nF ceramic per VDD/AVDD pin | |

**Pin assignments**

| PIC32MX pin | Function |
|---|---|
| RB0 | MCP4921 /CS (GPIO output) |
| RB3 | SCK1 (SPI clock) |
| RB5 | SDO1 (SPI MOSI) |
| RB13 | U1RX (MIDI input) |
| RA0 | Activity LED (toggles on every incoming MIDI byte) |

---

## Tech stack

- Language: C (XC32) + MIPS32r2 assembly (XC32 GNU assembler)
- Toolchain: MPLAB X IDE with XC32 v4.x compiler
- Target: PIC32MX270F256B at 80 MHz (FRC × PLL), PBCLK 40 MHz
- Simulator: MPLAB X simulator can exercise the MIDI parser and ADSR logic;
  the SPI DAC output can be verified with a logic analyzer or Proteus
- Test host: Python 3 with pyserial (`tools/midi_test.py`)

---

## Building

1. Open MPLAB X, create a new standalone project for PIC32MX270F256B.
2. Select XC32 as the toolchain.
3. Add all `.c` files from `firmware/` to **Source Files**.
4. Add `firmware/mips_dsp.S` to **Source Files** (XC32 processes `.S` through
   the C preprocessor then hands it to the assembler automatically).
5. Add all `.h` files from `firmware/` to **Header Files**.
6. Set optimization to `-O2` and target ISA to `-mips32r2`.
7. Build and program with PICkit 3/4 or SNAP.

No external libraries are needed beyond the XC32 standard library.

Command-line build (XC32 v6.x with the PIC32MX Device Family Pack):

```sh
xc32-gcc -mprocessor=32MX270F256B -mdfp="<PIC32MX_DFP>" -O2 \
    firmware/*.c firmware/mips_dsp.S -o fm-synth.elf
xc32-bin2hex fm-synth.elf
```

---

## MIDI control

Connect any MIDI keyboard, DAW or hardware sequencer to the DIN-5 input.

| CC number | Parameter | Range |
|---|---|---|
| CC 1 | FM modulation index | 0 (sine) to 127 (heavy FM) |
| CC 5 | Attack time | 0 to 127 → 0 to 4 000 ms |
| CC 6 | Decay time | 0 to 127 → 0 to 2 000 ms |
| CC 7 | Sustain level | 0 to 127 (linear amplitude) |
| CC 8 | Release time | 0 to 127 → 0 to 4 000 ms |
| CC 9 | LFO rate | 0 to 127 → 0.1 Hz to 20 Hz |
| CC 10 | LFO depth | 0 to 127 |
| CC 11 | Operator ratio | 0 (0.25:1) to 127 (4:1) |
| CC 80 | LFO target | < 64 = vibrato, ≥ 64 = tremolo |

---

## Test scripts

```bash
# Install dependency
pip install pyserial

# Run all demos
python3 tools/midi_test.py /dev/ttyUSB0

# Run a specific demo
python3 tools/midi_test.py /dev/ttyUSB0 --demo fm
python3 tools/midi_test.py /dev/ttyUSB0 --demo vibrato
python3 tools/midi_test.py /dev/ttyUSB0 --demo ratio

# Regenerate precomputed tables (optional)
python3 tools/gen_tables.py
```

Available demos: `scale`, `fm`, `ratio`, `vibrato`, `tremolo`, `adsr`, `chord`, `all`.

---

## Results

- Sample rate: 22 050 Hz, steady (Timer2 jitter < 1 cycle at 80 MHz)
- DAC resolution: 12 bits → 72 dB theoretical dynamic range
- Frequency accuracy: DDS resolution 0.005 mHz — tuning error below 0.001 cents
- ISR execution time: ≈ 380 cycles worst case out of 3 628 available (10.5%)
- Sine table THD: < -60 dBc with 8-bit linear interpolation
- Flash footprint: ≈ 14 KB code + 2 KB sine table + 512 B MIDI phase table
- RAM footprint: ≈ 400 bytes (voice state, stack, peripheral buffers)

---

## Signal chain reference

```
MIDI note  ──► midi_phase_inc[]  ──► DDS phase increment
                                         │
                              ┌──────────┴──────────┐
                              │ modulator osc        │ carrier osc
                              │ wavetable_interp()   │
                              │ × mod_index          │
                              └──────────────────────┘
                                         │ phase offset
                                         ▼
                                  wavetable_interp()  ← FM output
                                         │
                                    adsr_tick()       ← amplitude envelope
                                         │
                                    lfo_tick()        ← vibrato or tremolo
                                         │
                                  velocity scale
                                         │
                                    q15_to_dac()
                                         │
                                     MCP4921
                                         │
                                   analog audio out
```
