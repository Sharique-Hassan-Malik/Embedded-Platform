"""
Beer-Lambert law fitting and calibration utilities.

Beer-Lambert law:  A = ε · c · l

    A  = absorbance (dimensionless)
    ε  = molar attenuation coefficient (L mol⁻¹ cm⁻¹)
    c  = concentration (mol L⁻¹)
    l  = path length (cm) — fixed at 1.0 cm for a standard cuvette

A plot of A vs. c should be linear. Linear regression through the origin gives
the slope ε·l, from which an unknown concentration can be computed as c = A / (ε·l).
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Sequence

import numpy as np
from scipy import stats


@dataclass
class CalibrationResult:
    slope: float          # ε·l  (L mol⁻¹, since l=1 cm)
    intercept: float      # ideally 0 for a proper Beer-Lambert fit
    r_squared: float      # goodness of fit
    epsilon: float        # molar attenuation coefficient ε (L mol⁻¹ cm⁻¹)
    path_length_cm: float


@dataclass
class FitResult:
    concentrations: np.ndarray   # input concentrations (mol/L)
    absorbances: np.ndarray      # measured absorbances
    calibration: CalibrationResult
    residuals: np.ndarray        # A_measured − A_predicted


def fit_beer_lambert(
    concentrations: Sequence[float],
    absorbances: Sequence[float],
    path_length_cm: float = 1.0,
    force_origin: bool = False,
) -> FitResult:
    """
    Fit A = ε·c·l to calibration data using linear regression.

    Parameters
    ----------
    concentrations
        Known concentrations of calibration solutions (mol/L).
    absorbances
        Measured absorbances for each solution.
    path_length_cm
        Cuvette path length in centimetres (default 1.0).
    force_origin
        If True, force the regression through A=0 at c=0.
        Physically correct for Beer-Lambert but obscures systematic offset errors.

    Returns
    -------
    FitResult with calibration parameters and residuals.
    """
    c = np.asarray(concentrations, dtype=float)
    A = np.asarray(absorbances,    dtype=float)

    if len(c) != len(A):
        raise ValueError("concentrations and absorbances must have the same length")
    if len(c) < 2:
        raise ValueError("at least two calibration points are required")

    if force_origin:
        # OLS through origin: slope = (c·A) / (c·c)
        slope     = float(np.dot(c, A) / np.dot(c, c))
        intercept = 0.0
        A_pred    = slope * c
        ss_res    = float(np.sum((A - A_pred) ** 2))
        ss_tot    = float(np.sum((A - A.mean()) ** 2))
        r_squared = 1.0 - ss_res / ss_tot if ss_tot > 0 else 1.0
    else:
        result    = stats.linregress(c, A)
        slope     = float(result.slope)
        intercept = float(result.intercept)
        r_squared = float(result.rvalue ** 2)
        A_pred    = slope * c + intercept

    epsilon   = slope / path_length_cm
    residuals = A - A_pred

    cal = CalibrationResult(
        slope=slope,
        intercept=intercept,
        r_squared=r_squared,
        epsilon=epsilon,
        path_length_cm=path_length_cm,
    )
    return FitResult(
        concentrations=c,
        absorbances=A,
        calibration=cal,
        residuals=residuals,
    )


def concentration_from_absorbance(
    absorbance: float,
    calibration: CalibrationResult,
) -> float:
    """
    Invert the Beer-Lambert calibration to find an unknown concentration.

    c = (A − intercept) / (ε · l)
    """
    if math.isnan(absorbance):
        return math.nan
    denom = calibration.slope
    if abs(denom) < 1e-12:
        return math.nan
    return (absorbance - calibration.intercept) / denom
