# Wiring Guide

## Bill of Materials

| Qty | Component | Notes |
|---|---|---|
| 1 | Arduino Uno or Nano | ATmega328P |
| 1 | LED (white or monochromatic) | 5 mm, wavelength matched to analyte |
| 1 | Photodiode (BPW34 or SFH203) | ~600–950 nm sensitivity range |
| 1 | LM358 op-amp (or MCP6002) | For transimpedance amplifier |
| 1 | 1 MΩ resistor | TIA feedback (Rf) — adjust for signal range |
| 1 | 10 pF capacitor | TIA stability (in parallel with Rf) |
| 1 | 68 Ω resistor | LED current limiting |
| 1 | 10 kΩ potentiometer (optional) | LED intensity fine-tune |
| 1 | Standard 10 mm cuvette | 1.5 mL volume |
| 1 | Cuvette holder (3D-printed or commercial) | Dark enclosure critical |
| 1 | Microswitch or reed switch | Cuvette detect on D2 |
| — | Black card or shrink tubing | Stray light baffle |

---

## Transimpedance Amplifier (TIA)

The photodiode produces a photocurrent proportional to incident light intensity.
The TIA converts this to a voltage the ADC can measure.

```
              Rf (1 MΩ)
         ┌───┤├──────────────┐
         │   Cf (10 pF)      │
         │                   │
  PD  ───┤ (−)  LM358 ├──────┴──── V_out → A0
  GND ───┤ (+)          │
         │              │
         └──── GND (virtual) ──── GND
```

- `(+)` input tied to GND
- Photodiode anode to `(−)` input, cathode to 5 V (reverse bias improves speed)
- `V_out = −Rf × I_pd` (negative for reverse-biased anode-to-input configuration)
- If V_out is negative, swap the photodiode orientation or swap anode/cathode
- `Rf = 1 MΩ` gives ~0.5–3 V range for typical photodiode currents through
  standard 10 mm cuvettes. Reduce to 100 kΩ if signal saturates.

The 10 pF feedback capacitor prevents oscillation; without it the TIA may ring
at high frequencies.

---

## LED Driver (D9)

```
D9 ──[68 Ω]──[LED anode]──[LED cathode]── GND
```

Arduino D9 is OC1A (Timer1 PWM). The firmware uses `analogWrite(LED_PIN, duty)`
which generates an 8-bit PWM signal. The LED intensity is adjustable via the
`L<nnn>` serial command or the `--duty` flag.

Start with `duty = 180`. Verify that the photodiode output with a blank cuvette
is roughly 40–80% of the ADC full scale (410–820 counts) — this maximises
linear dynamic range for absorbance measurement.

---

## Cuvette Holder

The holder must:
- Position the LED, cuvette and photodiode in a fixed straight line
- Exclude all ambient light (use black card baffles or shrink tubing)
- Accept a standard 10 × 10 mm cuvette with repeatable positioning

A simple design using 3D-printed PLA with a 10 mm slot and a lid that presses
the cuvette against a reference face is sufficient. Wrap the exterior in black
electrical tape or spray-paint matte black.

The cuvette detect switch (microswitch or magnetic reed switch) is optional but
enables the firmware's `STATUS` command to report whether a cuvette is seated.

---

## Pin Summary

| Signal | Pin | Direction |
|---|---|---|
| LED drive | D9 | OUTPUT (PWM) |
| Signal photodiode | A0 | INPUT (analog) |
| Reference photodiode | A1 | INPUT (analog, optional) |
| Cuvette detect | D2 | INPUT_PULLUP (LOW = present) |
| LED enable switch | D3 | INPUT_PULLUP (LOW = LED off) |
| Status LED | D13 | OUTPUT |

---

## Calibration Solutions

To calibrate the instrument you need a series of solutions spanning the expected
concentration range. For a demonstration calibration using potassium permanganate
(KMnO₄, visible absorption at ~525 nm):

| Standard | KMnO₄ (mmol/L) | Notes |
|---|---|---|
| Blank | 0 | Deionised water |
| Std 1 | 0.10 | ~A = 0.05 (dilute) |
| Std 2 | 0.25 | |
| Std 3 | 0.50 | |
| Std 4 | 0.75 | |
| Std 5 | 1.00 | ~A = 0.50 at 525 nm |

Prepare from a 10 mmol/L stock by volumetric dilution. Use an LED with peak
emission near 525 nm (green) for KMnO₄. For other analytes, match the LED
wavelength to the analyte's absorption maximum.

---

## Stray Light

Stray light — ambient light reaching the photodiode without passing through the
sample — causes the Beer-Lambert calibration to curve downward at high absorbances
(A > 1.5). Minimise stray light by:

1. Working in a dim room or covering the instrument with a dark cloth
2. Sealing all gaps in the cuvette holder with black tape
3. Adding a baffle tube between the LED and the cuvette entrance aperture
