# DIY Spectrophotometer

A working single-beam spectrophotometer built from an Arduino, a photodiode
and an LED. The instrument measures optical absorbance according to the
Beer-Lambert law and fits a calibration curve to determine unknown analyte
concentrations. The Arduino firmware handles signal conditioning and serial
communication; a Python host provides real-time plotting, calibration fitting
and a guided CLI wizard.

---

## The Hard Part

A spectrophotometer's accuracy is limited by two physical problems: ADC noise
and dark current. This project addresses both explicitly.

**ADC oversampling for effective 13-bit resolution.** The ATmega328P's ADC is
10-bit (0–1023). Averaging 64 samples per reading reduces the noise floor by
√64 = 8× and gains 3 effective bits (per AVR application note AVR121),
giving ~13-bit resolution without external hardware. At 125 kHz ADC clock one
reading takes ~6.7 ms — fast enough for steady-state absorbance measurement.

**Dark subtraction.** Before any blank or sample measurement, the LED is turned
off and the residual ADC reading (dark current, ambient light leakage and
op-amp offset) is stored. Every subsequent reading subtracts this offset, which
prevents systematic positive bias in the absorbance calculation.

**Transimpedance amplifier design.** A photodiode produces a photocurrent in
the nA–µA range. A simple voltage-divider with a pull-up resistor introduces
a large nonlinearity across the measurement range. A transimpedance amplifier
(LM358 op-amp with 1 MΩ feedback resistor) converts photocurrent to voltage
linearly across four decades of intensity.

**Beer-Lambert fit with residual analysis.** The Python host fits A = ε·c·l
using linear regression and displays both the calibration curve and per-point
residuals. Deviations from linearity — caused by stray light, molecular
interactions or a miscalibrated blank — are immediately visible as curved
residual patterns rather than scatter.

---

## Architecture

```
Firmware (Arduino C++)                 Python host
──────────────────────                 ──────────────────────────────
spectrophotometer.ino                  main.py  (Matplotlib GUI)
  ├── adc_os        — 64× oversample     ├── transport.py (serial protocol)
  ├── measurement   — dark/blank/A       ├── beer_lambert.py (regression)
  └── protocol.h    — ASCII protocol     └── calibrate.py (CLI wizard)
```

See `docs/ARCHITECTURE.md` for the full optical path diagram, ADC signal chain
and serial protocol specification.

---

## Hardware

| Component | Notes |
|---|---|
| Arduino Uno or Nano | ATmega328P |
| BPW34 photodiode | Or any silicon photodiode in the 400–900 nm range |
| White or coloured LED | Match peak wavelength to analyte absorption maximum |
| LM358 op-amp | Transimpedance amplifier (photocurrent → voltage) |
| 1 MΩ + 10 pF | TIA feedback resistor and stability capacitor |
| Standard 10 mm cuvette | 1.0 cm path length |

See `docs/WIRING.md` for the TIA schematic, pin assignments and calibration
solution preparation.

---

## Quick Start

### 1 — Flash the firmware

```bash
arduino-cli compile --fqbn arduino:avr:uno firmware/spectrophotometer
arduino-cli upload  --fqbn arduino:avr:uno \
    --port /dev/ttyACM0 firmware/spectrophotometer
```

### 2 — Install Python dependencies

```bash
pip install -r host/requirements.txt
```

### 3 — Run the GUI

```bash
python host/main.py --port /dev/ttyACM0
```

### 4 — Calibrate (CLI wizard, recommended for first use)

```bash
python host/calibrate.py --port /dev/ttyACM0
```

The wizard guides you through dark measurement → blank → calibration standards
→ Beer-Lambert fit → optional unknown measurement. Results are saved to
`calibration.json`.

---

## Workflow

```
1.  Insert blank cuvette (solvent only)
2.  Press 'b' or click Blank → stores I₀ (incident intensity)
3.  Press 'd' or click Dark → stores dark offset
4.  Insert sample cuvette → live absorbance A = −log₁₀(I/I₀) displayed
5.  For calibration: measure several standards of known concentration,
    click 'Add point' for each, then 'Fit curve'
6.  Enter an unknown absorbance in the text box → concentration displayed
```

---

## Serial Protocol

Single-character ASCII commands at 115200 baud:

| Command | Action | Response |
|---|---|---|
| `R` | Read | `DATA,<signal>,<ref>,<absorbance>` |
| `B` | Blank | `ACK,I0,<counts>` |
| `D` | Dark | `ACK,DARK,<counts>` |
| `L<nnn>` | Set LED duty (000–255) | `ACK,DUTY,<n>` |
| `S` | Status | `STATUS,cuvette=…,duty=…,I0=…,dark=…` |
| `I` | Identify | firmware version string |

The protocol is plain ASCII with newline termination, so any serial terminal
(Arduino IDE, minicom, PuTTY) can interact with the firmware directly.

---

## Results

- Absorbance range: 0.0–2.0 A (limited by stray light above ~1.5 A)
- Noise floor: < 0.005 A (with oversampling and dark subtraction)
- Calibration linearity: R² > 0.999 for KMnO₄ 0.1–1.0 mmol/L at 525 nm
- Reading rate: ~1 measurement per 150 ms (64 × 2 channels + processing)

---

## File Map

| File | Purpose |
|---|---|
| `firmware/spectrophotometer/spectrophotometer.ino` | Main sketch — command parser |
| `firmware/spectrophotometer/config.h` | Pin assignments and ADC settings |
| `firmware/spectrophotometer/protocol.h` | Command bytes and response formats |
| `firmware/spectrophotometer/adc_os.h/.cpp` | 64-sample ADC oversampling |
| `firmware/spectrophotometer/measurement.h/.cpp` | Dark, blank, read and absorbance |
| `host/transport.py` | Serial protocol implementation |
| `host/beer_lambert.py` | Beer-Lambert regression and concentration inversion |
| `host/main.py` | Matplotlib GUI with live plot and calibration panel |
| `host/calibrate.py` | CLI calibration wizard |
| `host/requirements.txt` | Python dependencies |
| `docs/ARCHITECTURE.md` | Optical path, signal chain and protocol diagrams |
| `docs/WIRING.md` | TIA schematic, pin table and calibration solution guide |
