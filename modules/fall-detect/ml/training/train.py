"""
Train a 1D-CNN fall detection classifier and export to TFLite Micro.

Usage:
    python train.py                            # uses data/ directory
    python train.py --data-dir /path/to/data   # custom data path
    python train.py --epochs 40 --batch 64
    python train.py --augment                  # enable time-warp augmentation

Outputs:
    model.h5          — Keras model (for inspection and fine-tuning)
    model.tflite      — full-precision TFLite model
    model_int8.tflite — int8-quantised TFLite model (recommended for deployment)
    training_stats.json — mean/std used for normalisation
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent.parent))
from data.dataset import load_combined, compute_stats, WINDOW_LEN, WINDOW_AXES

try:
    import tensorflow as tf
    from tensorflow import keras
except ImportError:
    print("TensorFlow not installed.  Run: pip install tensorflow")
    sys.exit(1)


# ── Model ─────────────────────────────────────────────────────────────────

def build_model(input_shape: tuple[int, int]) -> keras.Model:
    """
    1D-CNN architecture optimised for deployment on Cortex-M4 (Arduino Nano 33 BLE).

    Design choices:
    - Two Conv1D layers with small kernels capture both short and medium
      temporal patterns.  Kernel size 5 at 100 Hz covers 50 ms — enough to
      detect the abrupt acceleration transient at impact.
    - GlobalAveragePooling1D instead of Flatten significantly reduces the
      parameter count and makes the model input-length agnostic.
    - Dropout at 0.4 on the dense layer prevents overfit on the relatively
      small fall datasets.
    - Final Softmax produces well-calibrated probabilities used for the
      confidence threshold on the device.

    Parameters: ~6 400 (float32) → ~1 600 bytes (int8-quantised)
    """
    inp = keras.Input(shape=input_shape, name="imu_window")

    x = keras.layers.Conv1D(16, kernel_size=5, padding="same",
                             activation="relu", name="conv1")(inp)
    x = keras.layers.BatchNormalization(name="bn1")(x)

    x = keras.layers.Conv1D(32, kernel_size=3, padding="same",
                             activation="relu", name="conv2")(x)
    x = keras.layers.BatchNormalization(name="bn2")(x)

    x = keras.layers.GlobalAveragePooling1D(name="gap")(x)

    x = keras.layers.Dense(32, activation="relu", name="fc1")(x)
    x = keras.layers.Dropout(0.4, name="drop")(x)

    out = keras.layers.Dense(2, activation="softmax", name="output")(x)

    return keras.Model(inputs=inp, outputs=out, name="fall_detector")


# ── Augmentation ──────────────────────────────────────────────────────────

def augment_batch(X: np.ndarray, y: np.ndarray,
                  rng: np.random.Generator) -> tuple[np.ndarray, np.ndarray]:
    """
    Light augmentation for the training set:
    - Gaussian noise (σ = 0.02 g / 0.5 °/s) simulates sensor noise.
    - Amplitude scaling in [0.9, 1.1] simulates different device placements.
    - Axis swap (x↔y) simulates device rotation by 90° around z.
    Only fall windows are augmented to balance the dataset.
    """
    X_aug, y_aug = [X.copy()], [y.copy()]

    fall_idx = np.where(y == 1)[0]
    X_falls  = X[fall_idx]

    noise_std = np.array([0.02, 0.02, 0.02, 0.5, 0.5, 0.5], dtype=np.float32)
    n_aug = len(fall_idx) * 3   # triple the fall count

    for _ in range(n_aug):
        idx = rng.integers(len(X_falls))
        w   = X_falls[idx].copy()

        # Gaussian noise
        w += rng.normal(0, noise_std, w.shape).astype(np.float32)

        # Amplitude scaling
        scale = rng.uniform(0.9, 1.1)
        w *= scale

        # Random axis swap (x↔y) with 50% probability
        if rng.random() > 0.5:
            w[:, [0, 1]] = w[:, [1, 0]]
            w[:, [3, 4]] = w[:, [4, 3]]

        X_aug.append(w[None])
        y_aug.append(np.array([1], dtype=np.int32))

    return (np.concatenate(X_aug, axis=0),
            np.concatenate(y_aug, axis=0))


# ── Training ──────────────────────────────────────────────────────────────

def train(args: argparse.Namespace) -> None:
    print("Loading dataset...")
    X, y = load_combined(args.data_dir)
    print(f"Total: {len(X)} windows  ({y.sum()} falls, {(y==0).sum()} ADL)")

    # Normalise per axis.
    mean, std = compute_stats(X)
    std = np.where(std < 1e-6, 1.0, std)   # avoid division by zero
    X = (X - mean) / std

    stats = {
        "mean": mean.tolist(),
        "std":  std.tolist(),
        "n_samples": int(len(X)),
        "n_falls":   int(y.sum()),
        "n_adl":     int((y == 0).sum()),
    }
    with open("training_stats.json", "w") as f:
        json.dump(stats, f, indent=2)
    print("Saved training_stats.json")

    # Train/val/test split (70 / 15 / 15).
    rng = np.random.default_rng(42)
    n   = len(X)
    idx = rng.permutation(n)
    n_train = int(0.70 * n)
    n_val   = int(0.15 * n)

    X_train, y_train = X[idx[:n_train]],         y[idx[:n_train]]
    X_val,   y_val   = X[idx[n_train:n_train+n_val]], y[idx[n_train:n_train+n_val]]
    X_test,  y_test  = X[idx[n_train+n_val:]],   y[idx[n_train+n_val:]]

    if args.augment:
        X_train, y_train = augment_batch(X_train, y_train, rng)
        print(f"After augmentation: {len(X_train)} training samples")

    # Class weights to handle imbalance.
    n_neg = int((y_train == 0).sum())
    n_pos = int((y_train == 1).sum())
    class_weight = {0: 1.0, 1: n_neg / max(n_pos, 1)}
    print(f"Class weight for falls: {class_weight[1]:.2f}")

    model = build_model((WINDOW_LEN, WINDOW_AXES))
    model.summary()

    model.compile(
        optimizer=keras.optimizers.Adam(learning_rate=1e-3),
        loss="sparse_categorical_crossentropy",
        metrics=["accuracy"],
    )

    callbacks = [
        keras.callbacks.EarlyStopping(
            monitor="val_accuracy", patience=8, restore_best_weights=True),
        keras.callbacks.ReduceLROnPlateau(
            monitor="val_loss", factor=0.5, patience=4, min_lr=1e-5),
        keras.callbacks.ModelCheckpoint(
            "model_best.h5", save_best_only=True, monitor="val_accuracy"),
    ]

    model.fit(
        X_train, y_train,
        epochs=args.epochs,
        batch_size=args.batch,
        validation_data=(X_val, y_val),
        class_weight=class_weight,
        callbacks=callbacks,
        verbose=1,
    )

    # Evaluation.
    print("\n── Test set evaluation ──")
    y_pred = np.argmax(model.predict(X_test, verbose=0), axis=1)
    tp = int(((y_pred == 1) & (y_test == 1)).sum())
    fp = int(((y_pred == 1) & (y_test == 0)).sum())
    tn = int(((y_pred == 0) & (y_test == 0)).sum())
    fn = int(((y_pred == 0) & (y_test == 1)).sum())
    accuracy    = (tp + tn) / len(y_test)
    sensitivity = tp / max(tp + fn, 1)   # fall recall
    specificity = tn / max(tn + fp, 1)   # ADL recall

    print(f"Accuracy:    {accuracy:.4f}")
    print(f"Sensitivity: {sensitivity:.4f}  (fall recall)")
    print(f"Specificity: {specificity:.4f}  (ADL recall)")
    print(f"TP={tp}  FP={fp}  TN={tn}  FN={fn}")

    model.save("model.h5")
    print("Saved model.h5")

    # Full-precision TFLite export.
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    tflite_model = converter.convert()
    with open("model.tflite", "wb") as f:
        f.write(tflite_model)
    print(f"Saved model.tflite ({len(tflite_model):,} bytes)")

    # Int8 quantised export — required for TFLite Micro.
    def representative_dataset():
        for i in range(min(200, len(X_train))):
            yield [X_train[i:i+1]]

    converter_q = tf.lite.TFLiteConverter.from_keras_model(model)
    converter_q.optimizations = [tf.lite.Optimize.DEFAULT]
    converter_q.representative_dataset = representative_dataset
    converter_q.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter_q.inference_input_type  = tf.int8
    converter_q.inference_output_type = tf.int8

    tflite_q = converter_q.convert()
    with open("model_int8.tflite", "wb") as f:
        f.write(tflite_q)
    print(f"Saved model_int8.tflite ({len(tflite_q):,} bytes)")


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description="Train fall detection classifier")
    ap.add_argument("--data-dir", default=str(Path(__file__).parent.parent / "data"),
                    help="Root directory containing SisFall/ and/or MobiAct/")
    ap.add_argument("--epochs",  type=int, default=30)
    ap.add_argument("--batch",   type=int, default=32)
    ap.add_argument("--augment", action="store_true",
                    help="Enable fall window augmentation (3× fall count)")
    return ap.parse_args()


if __name__ == "__main__":
    train(parse_args())
