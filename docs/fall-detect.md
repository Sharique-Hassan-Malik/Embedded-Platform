# Architecture — Wearable Fall Detection Device

## Overview

The system detects falls using a three-stage pipeline running entirely on-device:
a threshold pre-filter, a TFLite Micro 1D-CNN classifier and a posture check.
An SMS alert is sent via a SIM800L GSM module on confirmed falls. No cloud
connectivity or smartphone pairing is required.

---

## Hardware Block Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│  Arduino Nano 33 BLE  (nRF52840, 64 MHz Cortex-M4, 256 KB RAM) │
│                                                                  │
│  ┌──────────────┐  I2C 400 kHz  ┌──────────────────────────┐   │
│  │  MPU-6050    │───────────────▶│  mpu6050_read()          │   │
│  │  6-axis IMU  │  INT → D2     │  100 Hz DATA_RDY ISR     │   │
│  └──────────────┘               └─────────────┬────────────┘   │
│                                               │ Mpu6050Sample   │
│                                               ▼                 │
│                                  ┌─────────────────────────┐   │
│                                  │  fd_update() @ 100 Hz   │   │
│                                  │  Stage 1: SMV threshold  │   │
│                                  │  Stage 2: TFLite Micro   │   │
│                                  │  Stage 3: Posture check  │   │
│                                  └─────────────┬───────────┘   │
│                                               │ fall_confirmed  │
│                                               ▼                 │
│  ┌──────────────┐  SoftSerial   ┌─────────────────────────┐   │
│  │  SIM800L GSM │◀──────────────│  sim800_send_sms()       │   │
│  │  + SIM card  │  9600 baud    │  AT command driver       │   │
│  └──────────────┘               └─────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

---

## Detection Pipeline

### Stage 1 — Threshold Pre-Filter

Runs on every IMU sample at 100 Hz. Computes the Signal Magnitude Vector (SMV):

```
SMV = sqrt(ax² + ay² + az²)
```

A fall candidate opens when the SMV drops below the free-fall threshold (0.5 g),
indicating a near-weightless phase. The impact threshold (3.0 g) must then be
exceeded within a 500 ms window. This two-event gate reduces Stage 2 invocations
from 100 per second to roughly 1 per fall event, keeping average current
consumption close to the sensor-idle baseline.

State machine:

```
IDLE
 │  SMV < 0.5 g
 ▼
FREE_FALL
 │  SMV > 3.0 g within 500 ms           │  timeout (500 ms)
 ▼                                       ▼
IMPACT ────────────────────────────► IDLE
 │  200 ms elapsed (post-impact window)
 ▼
CLASSIFYING
 │  classifier run (see Stage 2)
 ├─ fall_prob < 0.75 ──────────────► IDLE
 ▼
LYING
 │  |az| < 0.5 g for ≥ 1000 ms          │  |az| > 0.5 g (person got up)
 ▼                                       ▼
ALERT ──────────────────────────────► COOLDOWN (30 s)
 │  send SMS, enter cooldown
 ▼
COOLDOWN ──── expires (30 s) ────► IDLE
```

### Stage 2 — TFLite Micro 1D-CNN Classifier

The 50-sample window (500 ms at 100 Hz) stored in the circular buffer is
fed to a quantised 1D-CNN. The window straddles the impact instant — the
post-impact collection time ensures both the free-fall and recovery phases
are captured.

Input pre-processing: each of the six axes is whitened using per-axis mean
and standard deviation computed from the training set. The statistics are
stored as C float arrays in `model_data.h` and applied in `fd_classify()`.

Model architecture:

```
Input:  [1, 50, 6]  float32 (normalised)
Conv1D:  16 filters, kernel 5, same padding, ReLU, BatchNorm
Conv1D:  32 filters, kernel 3, same padding, ReLU, BatchNorm
GlobalAveragePooling1D
Dense:   32 units, ReLU, Dropout 0.4
Dense:   2 units, Softmax
Output: [1, 2]  (p_not_fall, p_fall)
```

Parameters: ~6 400 (float32) → ~1 600 bytes (int8-quantised).
Inference time on nRF52840 @ 64 MHz: ~18 ms.
Reported accuracy on SisFall + MobiAct test split: 94.3%.
Sensitivity (fall recall): 96.1%. Specificity (ADL recall): 92.8%.

### Stage 3 — Posture Check

After a positive classifier result the detector waits 1000 ms. If the
vertical accelerometer component |az| stays below 0.5 g throughout —
indicating the device remains near-horizontal, consistent with lying on the
floor — the fall is confirmed. If the person gets up or the device returns
to a vertical orientation (|az| > 0.5 g) the result is classified as a
false positive and the alert is suppressed.

---

## IMU Configuration

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| Accel full scale | ±16 g | Captures impact peaks (typically 5–15 g) |
| Gyro full scale | ±2000 °/s | Captures fast rotation during falls |
| DLPF bandwidth | 44 Hz accel / 42 Hz gyro | Removes vibration above walking cadence |
| Sample rate | 100 Hz | 2× Nyquist for 44 Hz bandwidth |
| Clock source | PLL + X gyro | More stable than internal RC oscillator |

The MPU-6050 INT pin is connected to Arduino D2 (external interrupt) and
configured as an active-low data-ready output. The ISR sets a boolean flag;
the main loop reads the sample only when the flag is set rather than polling
the DATA_RDY bit over I2C, avoiding unnecessary bus transactions.

---

## SMS Alert Format

```
FALL DETECTED
Confidence: 92%
Falls total: 1
Time: 3742s
```

The SIM800L communicates over SoftwareSerial at 9600 baud (hardware UART is
occupied by the USB-serial bridge). The AT command driver follows the standard
three-phase flow: `AT+CMGS="<number>"` → wait for `>` prompt → send body +
Ctrl+Z → wait for `+CMGS:` acknowledgement.

The module requires a separate 3.7–4.2 V supply capable of 2 A peak. A
separate LiPo cell with a protection circuit module is the recommended
solution. Powering the SIM800L from the Arduino 5 V rail will cause voltage
droop during GPRS bursts that resets the module mid-transmission.

---

## Power Budget

| Condition | Current (approx) |
|-----------|-----------------|
| Idle (IMU polling, no GSM TX) | ~20 mA |
| Classifier running (18 ms burst) | ~25 mA |
| GSM registration / SMS TX | up to 2 A peak, ~500 mA avg |
| Deep sleep (not implemented) | < 1 mA achievable |

A 1000 mAh LiPo cell provides approximately 48 hours of continuous monitoring
without deep sleep.

---

## ML Training Pipeline

```
data/SisFall/        data/MobiAct/
      │                    │
      └────────┬───────────┘
               ▼
         dataset.py
         - Resample to 100 Hz
         - Centre window on impact peak (falls)
         - Sliding window stride 25 (ADL)
               │
               ▼
           train.py
         - Normalise (per-axis mean/std)
         - 70/15/15 train/val/test split
         - Class-weighted loss
         - EarlyStopping on val_accuracy
         - Int8 quantisation (TFLiteConverter)
               │
               ├── model_int8.tflite
               └── training_stats.json
               │
               ▼
        export_model.py
         - xxd-style byte array
         - Embed normalisation stats
               │
               ▼
    firmware/include/model_data.h
```

---

## File Map

| File | Description |
|------|-------------|
| `firmware/include/mpu6050.h` | MPU-6050 register map and driver API |
| `firmware/include/sim800l.h` | SIM800L AT command driver API |
| `firmware/include/fall_detect.h` | Fall detector FSM, thresholds and API |
| `firmware/include/model_data.h` | TFLite model byte array and normalisation stats |
| `firmware/src/mpu6050.cpp` | MPU-6050 driver (Wire API, burst read, self-test) |
| `firmware/src/sim800l.cpp` | SIM800L driver (SoftwareSerial, AT commands) |
| `firmware/src/fall_detect.cpp` | Three-stage FSM and TFLite Micro inference |
| `firmware/src/main.ino` | Arduino sketch, ISR, state logging, alert dispatch |
| `ml/data/dataset.py` | SisFall and MobiAct dataset loaders |
| `ml/training/train.py` | 1D-CNN training with augmentation and int8 export |
| `ml/training/export_model.py` | Converts TFLite model to C header |
| `host/monitor.py` | Serial monitor with colour-coded state display and CSV capture |
| `tests/test_fall_detect.py` | 27 pytest assertions |
