# Power Consumption Profiler

> Part of the [Embedded Platform](../../README.md) — nineteen firmware projects
> behind one build harness. This folder builds and runs on its own; `embed build
> --only power-profiler` builds it alongside the rest.

A sub-millisecond resolution current measurement tool for battery-powered
Arduino sketches. A profiler Arduino samples current through a shunt resistor
at 1000 S/s using an INA219 current-sense amplifier and streams binary packets
to a Python host at 1 Mbaud. The DUT (device under test) drives GPIO annotation
pins to mark code section boundaries; the host renders an annotated current
timeline and computes mean, peak, RMS and energy statistics per section.

---

## The Hard Part

**Sampling at 1000 S/s with INA219 over I2C inside a Timer1 ISR.** A naive
implementation would poll the INA219 in `loop()` with no timing guarantee.
This firmware programs Timer1 in CTC mode to fire at exactly 1 kHz; the ISR
reads the INA219 shunt voltage register over 400 kHz I2C (~55 µs), reads the
four annotation GPIO pins, pushes the sample into a ring buffer and returns —
all within the 1 ms inter-sample budget.

**Lock-free ring buffer for ISR-to-loop transfer.** The ISR (producer) and
`loop()` (consumer) share a power-of-2 ring buffer. Index updates are ordered
by write-then-increment (ISR) and read-then-increment (loop) with `cli()/sei()`
wrapping only the 16-bit index comparison — not the data copy — to minimise
interrupt latency.

**Binary protocol at 1 Mbaud with per-packet checksum and re-sync.** Each
5-byte sample packet carries an XOR checksum. The host parser implements a
two-state machine (HUNT → PAYLOAD) that re-synchronises on any checksum failure
without discarding subsequent valid packets. Choosing 1 Mbaud over 115200 gives
×87 headroom, keeping the serial link a non-bottleneck even when bursting
multiple samples per `loop()` iteration.

**Annotation-correlated statistics.** The host detects rising and falling edges
in the annotation bitmask stream and accumulates samples within each section
window. After each section closes, NumPy computes mean, peak, RMS current and
energy (using mean current × supply voltage × duration) without any per-sample
branching in the hot path.

---

## Architecture

```
Firmware (Arduino C++)
  power_profiler.ino — Timer1 ISR, ring buffer drain, serial framing
  ina219.h/.cpp      — INA219 12-bit current-sense driver from scratch
  ringbuf.h          — lock-free ISR→loop ring buffer

Python host
  transport.py       — binary state-machine parser, background reader thread
  capture.py         — accumulator, annotation edge detection, statistics
  plot.py            — live Matplotlib dashboard (trace + table + energy bars)
  main.py            — entry point, CLI, CSV export
```

See `docs/ARCHITECTURE.md` for the ISR timing budget, wire packet format,
ring buffer safety analysis and annotation protocol details.

---

## Hardware

| Component | Role |
|---|---|
| Arduino Uno/Nano (profiler) | Samples current, streams packets to PC |
| INA219 breakout | 12-bit current sense, ±40 mV shunt range |
| 0.1 Ω 1% shunt resistor | In series with DUT supply rail |
| Arduino Uno/Nano (DUT) | Device being profiled |
| Jumper wires | Annotation pin connections (D2–D5) |

See `docs/WIRING.md` for the full shunt insertion diagram, I2C wiring and
the direct-ADC alternative if no INA219 is available.

---

## Quickstart

### 1 — Flash firmware

```bash
# Profiler
arduino-cli compile --fqbn arduino:avr:uno firmware/power_profiler
arduino-cli upload  --fqbn arduino:avr:uno --port /dev/ttyACM0 firmware/power_profiler

# DUT example
arduino-cli compile --fqbn arduino:avr:uno firmware/example_dut
arduino-cli upload  --fqbn arduino:avr:uno --port /dev/ttyACM1 firmware/example_dut
```

### 2 — Install Python dependencies

```bash
pip install -r host/requirements.txt
```

### 3 — Run

```bash
python host/main.py --port /dev/ttyACM0 --supply 3300
```

### 4 — Save a CSV

```bash
python host/main.py --port /dev/ttyACM0 --duration 30 --out capture.csv
```

---

## Dashboard

Three panels update live at 5 Hz:

| Panel | Content |
|---|---|
| Current trace | Scrolling mA vs. time; annotation spans as coloured bands |
| Statistics table | Per-section mean, peak, RMS, duration and energy |
| Energy chart | Bar chart of µJ consumed per annotated section |

---

## Annotation Protocol

In the DUT sketch, toggle a GPIO pin to mark section boundaries:

```cpp
// Open section on channel 0
digitalWrite(2, HIGH);   // → profiler D2

// ... code being profiled ...

// Close section
digitalWrite(2, LOW);
```

Up to 4 sections can be open simultaneously on channels 0–3 (D2–D5).
Sections on different channels can be nested or overlapping.

---

## Results

- Sampling rate: 1000 S/s (Timer1 CTC, INA219 12-bit)
- Current resolution: 0.1 mA (INA219 at ±40 mV / 0.1 Ω shunt)
- Minimum measurable current: ~1 mA (INA219 noise floor)
- Serial throughput: 5000 bytes/s at 1000 S/s; 1 Mbaud gives ×87 headroom
- Annotation latency: one sample period (1 ms) from GPIO edge to host detection

---

## File Map

| File | Purpose |
|---|---|
| `firmware/power_profiler/power_profiler.ino` | Main profiler sketch |
| `firmware/power_profiler/config.h` | Pins, rate, INA219 settings |
| `firmware/power_profiler/ina219.h/.cpp` | INA219 driver from scratch |
| `firmware/power_profiler/ringbuf.h` | Lock-free ISR ring buffer |
| `firmware/example_dut/example_dut.ino` | Example annotated DUT sketch |
| `host/transport.py` | Binary packet parser and reader thread |
| `host/capture.py` | Sample accumulator and section statistics |
| `host/plot.py` | Matplotlib live dashboard |
| `host/main.py` | Entry point and CSV export |
| `docs/ARCHITECTURE.md` | ISR pipeline, packet format, ring buffer analysis |
| `docs/WIRING.md` | Shunt insertion, INA219 wiring, annotation connections |
