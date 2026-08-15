"""
Dataset loader for fall detection training.

Supported datasets:
  SisFall  — http://sistemic.udea.edu.co/en/research/projects/english-falls/
  MobiAct  — https://bmi.hmu.gr/the-mobifall-and-mobiact-datasets-2/

Both datasets provide CSV files with columns: [ax, ay, az, gx, gy, gz]
sampled at 200 Hz.  This loader resamples to 100 Hz and extracts labelled
windows of length WINDOW_SAMPLES (50 samples = 500 ms).

Expected directory layout:

  data/
    SisFall/
      SA01/
        D01_SA01_R01.txt    ← fall trial
        W01_SA01_R01.txt    ← ADL (Activities of Daily Living) trial
      ...
    MobiAct/
      falls/
        FOL_sub1_trial1.csv
        ...
      adl/
        STD_sub1_trial1.csv
        ...

Labels:
  1 — fall
  0 — not fall (ADL)

The window is centred on the sample with maximum SMV (impact peak) for fall
trials.  For ADL trials, windows are extracted with a 50% overlap stride.
"""

from __future__ import annotations

import os
import re
from pathlib import Path
from typing import Iterator

import numpy as np

WINDOW_LEN   = 50    # samples per window
WINDOW_AXES  = 6     # ax, ay, az, gx, gy, gz
STRIDE_ADL   = 25    # overlap stride for ADL windows
TARGET_HZ    = 100


def _resample(data: np.ndarray, src_hz: int) -> np.ndarray:
    """Downsample by integer factor if src_hz > TARGET_HZ."""
    if src_hz == TARGET_HZ:
        return data
    if src_hz % TARGET_HZ != 0:
        raise ValueError(f"Cannot downsample {src_hz} Hz to {TARGET_HZ} Hz exactly")
    factor = src_hz // TARGET_HZ
    return data[::factor]


def _smv(data: np.ndarray) -> np.ndarray:
    """Signal magnitude vector of the first three columns (accelerometer)."""
    return np.sqrt((data[:, :3] ** 2).sum(axis=1))


def _extract_fall_window(data: np.ndarray) -> np.ndarray | None:
    """Centre a single window on the impact peak (maximum SMV)."""
    smv = _smv(data)
    peak = int(np.argmax(smv))
    half = WINDOW_LEN // 2
    start = peak - half
    end   = peak + (WINDOW_LEN - half)
    if start < 0 or end > len(data):
        return None
    return data[start:end]


def _extract_adl_windows(data: np.ndarray) -> list[np.ndarray]:
    """Sliding window extraction for ADL trials."""
    windows = []
    for start in range(0, len(data) - WINDOW_LEN + 1, STRIDE_ADL):
        windows.append(data[start:start + WINDOW_LEN])
    return windows


def _parse_sisfall_file(path: Path, src_hz: int = 200) -> np.ndarray | None:
    """
    SisFall format: space-separated, columns ax ay az gx gy gz.
    Accelerometer in units of g/1000 (needs ÷1000).
    Gyroscope in units of 0.01 °/s (needs ÷100).
    """
    try:
        raw = np.loadtxt(path, delimiter=",")
    except Exception:
        return None
    if raw.ndim != 2 or raw.shape[1] < 6:
        return None
    data = raw[:, :6].astype(np.float32)
    data[:, :3] /= 1000.0   # → g
    data[:, 3:] /= 100.0    # → °/s
    return _resample(data, src_hz)


def _parse_mobiact_file(path: Path, src_hz: int = 200) -> np.ndarray | None:
    """
    MobiAct format: CSV with header.
    Columns: timestamp, acc_x, acc_y, acc_z, gyro_x, gyro_y, gyro_z, ...
    Accelerometer in m/s² (convert to g by dividing by 9.80665).
    Gyroscope in rad/s (convert to °/s by multiplying by 180/pi).
    """
    try:
        import csv
        rows = []
        with open(path, newline="") as f:
            reader = csv.DictReader(f)
            for row in reader:
                rows.append([
                    float(row["acc_x"])  / 9.80665,
                    float(row["acc_y"])  / 9.80665,
                    float(row["acc_z"])  / 9.80665,
                    float(row["gyro_x"]) * 57.2958,
                    float(row["gyro_y"]) * 57.2958,
                    float(row["gyro_z"]) * 57.2958,
                ])
        if not rows:
            return None
        data = np.array(rows, dtype=np.float32)
        return _resample(data, src_hz)
    except Exception:
        return None


def load_sisfall(root: str | Path) -> tuple[np.ndarray, np.ndarray]:
    """
    Load SisFall dataset.  Returns (X, y) with X shaped [N, WINDOW_LEN, WINDOW_AXES].
    File naming convention:
      D##_* → fall trial (label 1)
      W##_* → walking/ADL trial (label 0)
      F##_* → fall trial variant
      All others → ADL (label 0)
    """
    root = Path(root)
    windows, labels = [], []

    for subj_dir in sorted(root.iterdir()):
        if not subj_dir.is_dir():
            continue
        for f in sorted(subj_dir.iterdir()):
            if f.suffix.lower() not in (".txt", ".csv"):
                continue
            is_fall = bool(re.match(r"^[DF]\d", f.name, re.IGNORECASE))
            data = _parse_sisfall_file(f)
            if data is None or len(data) < WINDOW_LEN:
                continue
            if is_fall:
                w = _extract_fall_window(data)
                if w is not None:
                    windows.append(w)
                    labels.append(1)
            else:
                for w in _extract_adl_windows(data):
                    windows.append(w)
                    labels.append(0)

    if not windows:
        return np.empty((0, WINDOW_LEN, WINDOW_AXES)), np.empty(0, dtype=np.int32)

    return np.stack(windows).astype(np.float32), np.array(labels, dtype=np.int32)


def load_mobiact(root: str | Path) -> tuple[np.ndarray, np.ndarray]:
    """Load MobiAct dataset from falls/ and adl/ subdirectories."""
    root = Path(root)
    windows, labels = [], []

    for category, label in [("falls", 1), ("adl", 0)]:
        cat_dir = root / category
        if not cat_dir.exists():
            continue
        for f in sorted(cat_dir.iterdir()):
            if f.suffix.lower() != ".csv":
                continue
            data = _parse_mobiact_file(f)
            if data is None or len(data) < WINDOW_LEN:
                continue
            if label == 1:
                w = _extract_fall_window(data)
                if w is not None:
                    windows.append(w)
                    labels.append(1)
            else:
                for w in _extract_adl_windows(data):
                    windows.append(w)
                    labels.append(0)

    if not windows:
        return np.empty((0, WINDOW_LEN, WINDOW_AXES)), np.empty(0, dtype=np.int32)

    return np.stack(windows).astype(np.float32), np.array(labels, dtype=np.int32)


def load_combined(data_dir: str | Path) -> tuple[np.ndarray, np.ndarray]:
    """Load and merge all available datasets under data_dir."""
    data_dir = Path(data_dir)
    X_parts, y_parts = [], []

    sisfall_root = data_dir / "SisFall"
    if sisfall_root.exists():
        X, y = load_sisfall(sisfall_root)
        if len(X):
            X_parts.append(X)
            y_parts.append(y)
            print(f"SisFall: {len(X)} windows ({y.sum()} falls, {(y==0).sum()} ADL)")

    mobiact_root = data_dir / "MobiAct"
    if mobiact_root.exists():
        X, y = load_mobiact(mobiact_root)
        if len(X):
            X_parts.append(X)
            y_parts.append(y)
            print(f"MobiAct: {len(X)} windows ({y.sum()} falls, {(y==0).sum()} ADL)")

    if not X_parts:
        raise FileNotFoundError(
            f"No dataset found under {data_dir}.\n"
            "Download SisFall or MobiAct and place them in data/SisFall/ or data/MobiAct/."
        )

    X_all = np.concatenate(X_parts, axis=0)
    y_all = np.concatenate(y_parts, axis=0)

    rng = np.random.default_rng(42)
    idx = rng.permutation(len(X_all))
    return X_all[idx], y_all[idx]


def compute_stats(X: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Per-axis mean and std across the entire dataset for normalisation."""
    flat = X.reshape(-1, WINDOW_AXES)
    return flat.mean(axis=0), flat.std(axis=0)
