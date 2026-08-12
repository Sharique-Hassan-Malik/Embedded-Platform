# Architecture

## System Overview

```
                         ┌──────────────────────────────────┐
                         │           Cuvette holder          │
                         │                                   │
  LED (D9, PWM) ──────►  │ ──►  [sample]  ──►               │ ──► Photodiode (A0)
                         │                                   │
                         └──────────────────────────────────┘
                                         │
                                   TIA (op-amp)
                                         │
                               Arduino ADC (10-bit, oversampled)
                                         │
                              Serial ASCII protocol (115200 baud)
                                         │
                              Python host (transport.py)
                                         │
                          ┌──────────────┴─────────────────┐
                          │                                 │
                    main.py (GUI)               calibrate.py (CLI)
                          │                                 │
                   beer_lambert.py          beer_lambert.py
               (linear regression,          (same module)
                concentration inversion)
```

---

## Optical Path

A single LED illuminates the cuvette. The photodiode on the far side captures
transmitted light. A second reference photodiode (A1, optional) samples light
from the same source without passing through the sample — allowing ratiometric
correction for LED intensity drift.

```
LED → [cuvette: solvent + analyte] → photodiode (A0)
           ↑
    I0 measured here with blank (no analyte)
    I  measured here with sample
```

Absorbance:  `A = -log₁₀(I / I₀)`

---

## ADC Signal Chain

```
Photodiode
    │  (photocurrent, nA–µA range)
    ▼
Transimpedance Amplifier (LM358 or similar)
    │  converts current → voltage (gain = Rf, e.g. 1 MΩ)
    ▼
Arduino A0 pin (0–5 V input range)
    │
analogRead() × 64 oversampled samples
    │  sum / 64 → 10-bit averaged result
    ▼
Dark subtraction: net = raw − dark
    │
Beer-Lambert: A = -log₁₀(net / I₀)
```

### Oversampling

The ATmega328P ADC has 10-bit resolution (0–1023). By averaging 64 samples,
effective resolution increases by `log₂(64)/2 = 3` bits → 13-bit effective
resolution (per AVR121 application note). At 125 kHz ADC clock (prescaler 128):

- One conversion: 104 µs
- 64 conversions: ~6.7 ms per reading

This is acceptable for steady-state absorbance measurement.

### Dark Subtraction

The dark reading captures the ADC offset when the LED is off:
ambient light leakage, op-amp input offset, and ADC reference offset.
All subsequent readings subtract this value before computing absorbance,
preventing systematic positive bias.

---

## Serial Protocol

All communication is newline-terminated ASCII (115200 8N1).
The host sends one command character; the firmware responds with one packet.

### Commands

| Cmd | Sent by host | Firmware action |
|---|---|---|
| `R` | Read | Take one averaged reading; reply DATA |
| `B` | Blank | Store current reading as I₀; reply ACK |
| `D` | Dark | LED off, measure, LED on; reply ACK |
| `L<nnn>` | Set LED | Set PWM duty (000–255); reply ACK |
| `S` | Status | Reply STATUS packet |
| `I` | Identify | Reply firmware version string |

### Response formats

```
DATA,<signal>,<reference>,<absorbance>
ACK,I0,<counts>
ACK,DARK,<counts>
ACK,DUTY,<n>
STATUS,cuvette=<0|1>,duty=<n>,I0=<counts>,dark=<counts>
ERR,<reason>
```

`<absorbance>` is `nan` when no blank has been stored. The host treats this
as `math.nan`.

---

## Beer-Lambert Law

```
A = ε · c · l

A  = absorbance (dimensionless; measured)
ε  = molar attenuation coefficient (L mol⁻¹ cm⁻¹; material property)
c  = concentration (mol/L; unknown to be determined)
l  = path length (cm; fixed at 1.0 cm for a standard cuvette)
```

### Calibration

A series of solutions with known concentrations c₁ … cₙ are measured,
giving absorbances A₁ … Aₙ. Linear regression of A on c gives:

```
A = slope · c + intercept
slope = ε · l
```

For ideal Beer-Lambert behaviour, `intercept ≈ 0` and `R² ≈ 1.0`.

### Concentration inversion

```
c_unknown = (A_unknown − intercept) / slope
```

### Linearity limits

Beer-Lambert linearity breaks down at high concentrations (A > ~1.5) due to
molecular interactions and stray light. The calibration wizard prints residuals
so deviations from linearity are immediately visible.

---

## Software Layers

### Firmware (Arduino C++)

| File | Role |
|---|---|
| `config.h` | Pin assignments, ADC settings, serial baud |
| `protocol.h` | Command bytes and response format constants |
| `adc_os.h/.cpp` | Oversampled ADC (64-sample averaging) |
| `measurement.h/.cpp` | Dark/blank/read, LED control, absorbance computation |
| `spectrophotometer.ino` | Command parser and response serializer |

### Host (Python)

| File | Role |
|---|---|
| `transport.py` | Serial communication, response parsing |
| `beer_lambert.py` | Linear regression, calibration and concentration inversion |
| `main.py` | Matplotlib GUI: live plot, calibration panel, unknown readout |
| `calibrate.py` | CLI wizard: guided dark→blank→standards→fit→unknown workflow |
