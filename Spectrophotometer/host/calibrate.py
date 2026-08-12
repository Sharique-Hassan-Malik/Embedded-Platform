"""
Command-line calibration wizard for the spectrophotometer.

Guides the user through:
  1. Dark measurement
  2. Blank measurement
  3. Series of calibration standards (known concentration → measured A)
  4. Beer-Lambert fit with ε and R² output
  5. Optional unknown concentration measurement

Usage
-----
    python calibrate.py --port /dev/ttyACM0

Results are saved to calibration.json for use by main.py or offline analysis.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

from transport import Spectrophotometer
from beer_lambert import fit_beer_lambert, concentration_from_absorbance


def prompt_float(msg: str) -> float:
    while True:
        try:
            return float(input(msg).strip())
        except ValueError:
            print("  Please enter a valid number.")


def main() -> None:
    parser = argparse.ArgumentParser(description="Spectrophotometer calibration wizard")
    parser.add_argument("--port",   required=True)
    parser.add_argument("--baud",   type=int, default=115200)
    parser.add_argument("--output", default="calibration.json")
    parser.add_argument("--path-length", type=float, default=1.0,
                        help="Cuvette path length in cm (default 1.0)")
    args = parser.parse_args()

    print(f"Connecting to {args.port} …")
    with Spectrophotometer(args.port, args.baud) as inst:
        print(f"Firmware: {inst.identify()}")

        # ── Step 1: dark ──────────────────────────────────────────────────────
        input("\nRemove cuvette and press Enter to measure dark current…")
        dark = inst.dark()
        print(f"  Dark: {dark} ADC counts")

        # ── Step 2: blank ─────────────────────────────────────────────────────
        input("\nInsert BLANK cuvette (solvent only) and press Enter…")
        i0 = inst.blank()
        print(f"  Blank (I0): {i0} ADC counts")

        # ── Step 3: calibration standards ────────────────────────────────────
        concs: list[float] = []
        abs_vals: list[float] = []

        print("\nEnter calibration standards. Type 'done' when finished.")
        print("Each standard: insert cuvette → enter concentration → measure.\n")

        while True:
            raw = input("Concentration (mol/L) or 'done': ").strip().lower()
            if raw == "done":
                break
            try:
                c = float(raw)
            except ValueError:
                print("  Invalid number.")
                continue

            input(f"  Insert standard c={c} mol/L and press Enter…")
            reading = inst.read()

            if math.isnan(reading.absorbance):
                print("  No blank stored — cannot compute absorbance. Re-run blank.")
                continue

            print(f"  A = {reading.absorbance:.4f}")
            concs.append(c)
            abs_vals.append(reading.absorbance)

        if len(concs) < 2:
            print("\nNeed at least 2 calibration points. Aborting.")
            sys.exit(1)

        # ── Step 4: fit ───────────────────────────────────────────────────────
        print("\n── Beer-Lambert Fit ─────────────────────────────────────────────")
        result = fit_beer_lambert(concs, abs_vals, path_length_cm=args.path_length)
        cal    = result.calibration

        print(f"  Slope (ε·l):  {cal.slope:.4f}")
        print(f"  Intercept:    {cal.intercept:.4f}")
        print(f"  ε (molar attenuation): {cal.epsilon:.1f} L mol⁻¹ cm⁻¹")
        print(f"  R²:           {cal.r_squared:.6f}")
        print()
        print("  Concentration  Measured A  Predicted A  Residual")
        for c, A, res in zip(result.concentrations, result.absorbances, result.residuals):
            A_pred = cal.slope * c + cal.intercept
            print(f"  {c:12.6f}   {A:10.4f}  {A_pred:11.4f}  {res:+.4f}")

        # Save calibration
        cal_data = {
            "slope":          cal.slope,
            "intercept":      cal.intercept,
            "r_squared":      cal.r_squared,
            "epsilon":        cal.epsilon,
            "path_length_cm": cal.path_length_cm,
            "calibration_points": [
                {"concentration": c, "absorbance": A}
                for c, A in zip(concs, abs_vals)
            ],
        }
        Path(args.output).write_text(json.dumps(cal_data, indent=2))
        print(f"\nCalibration saved to {args.output}")

        # ── Step 5: optional unknown ──────────────────────────────────────────
        measure_unknown = input("\nMeasure an unknown sample? [y/N] ").strip().lower()
        if measure_unknown == "y":
            input("Insert unknown cuvette and press Enter…")
            reading = inst.read()
            if math.isnan(reading.absorbance):
                print("No blank stored — cannot compute concentration.")
            else:
                c_unk = concentration_from_absorbance(reading.absorbance, cal)
                print(f"  A = {reading.absorbance:.4f}  →  c = {c_unk:.6f} mol/L")


if __name__ == "__main__":
    main()
