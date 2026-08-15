# Wearable Fall Detection Device

> Part of the [Embedded Platform](../../README.md) — nineteen firmware projects
> behind one build harness. This folder builds and runs on its own; `embed build
> --only fall-detect` builds it alongside the rest.

An end-to-end embedded ML pipeline that detects falls using an MPU-6050 IMU on an
Arduino, runs a TFLite Micro 1D-CNN classifier on-device, and sends an SMS alert
via a SIM800L GSM module. No cloud dependency, no phone pairing.

---

## What it is

Falls are a leading cause of injury in the elderly. This device solves the problem
with a three-stage pipeline:

1. **Threshold pre-filter** — computes the Signal Magnitude Vector (SMV) at 100 Hz
   and gates the classifier on free-fall followed by impact, eliminating 99% of
   normal activity before the ML model runs.
2. **TFLite Micro 1D-CNN** — a 6 400-parameter classifier runs on a 500 ms window
   of 6-axis IMU data and outputs a fall probability. Runs in ~18 ms on a Cortex-M4.
3. **Posture check** — after a positive classification, the device confirms the
   person remains near-horizontal for 1 second before sending the alert. This
   suppresses false positives from vigorous activity.

Reported performance on SisFall + MobiAct datasets: 94.3% accuracy, 96.1%
sensitivity (fall recall) and 92.8% specificity (ADL recall).

---

## The hard part

**Two-event threshold gate.** A single acceleration threshold produces too many
false positives — jumping, sitting down quickly and dropping the device all exceed
3 g. The free-fall phase (SMV < 0.5 g) must precede the impact phase within 500 ms.
This ordered two-event gate is far more selective and runs in ~5 µs per sample,
so the 18 ms classifier is only invoked on genuine fall candidates.

**Circular buffer alignment for the classifier.** The classifier window must be
centred on the impact instant, not just the most recent 50 samples. The detector
waits 200 ms after the impact flag before invoking the classifier, so the buffer
contains 300 ms of pre-impact data (free-fall + impact peak) and 200 ms of
post-impact data (the critical recovery phase that distinguishes a fall from a
dropped device).

**Int8 quantisation for TFLite Micro.** The full-precision model is 25 KB.
Int8 quantisation reduces this to ~8 KB with a representative calibration dataset
derived from the training set. The normalisation statistics (per-axis mean and std)
must be applied in floating-point before the quantised model runs — TFLite Micro
does not fold normalisation into the graph automatically.

**SIM800L power supply isolation.** The GSM module draws up to 2 A during
transmission. Sharing a supply with the microcontroller causes voltage droop that
resets the module mid-transmission and corrupts the AT command sequence.
The design uses a separate LiPo cell for the SIM800L.

---

## Repository structure

```
fall-detect/
  firmware/
    include/
      mpu6050.h       — MPU-6050 register map, driver API
      sim800l.h       — SIM800L AT command driver API
      fall_detect.h   — FSM definition, thresholds, detector API
      model_data.h    — TFLite model byte array + normalisation stats
    src/
      mpu6050.cpp     — I2C driver (burst read, self-test)
      sim800l.cpp     — AT command driver (SoftwareSerial)
      fall_detect.cpp — three-stage FSM + TFLite Micro inference
      main.ino        — Arduino sketch: ISR, state machine, SMS dispatch
  ml/
    data/
      dataset.py      — SisFall and MobiAct loaders, windowing
    training/
      train.py        — 1D-CNN training, augmentation, int8 export
      export_model.py — TFLite → C header converter
  host/
    monitor.py        — serial monitor, colour-coded state display, CSV capture
  tests/
    test_fall_detect.py — 27 pytest assertions
  docs/
    architecture.md   — full pipeline description and design rationale
  requirements.txt
  .gitignore
```

---

## Hardware

| Component | Part | Notes |
|-----------|------|-------|
| Microcontroller | Arduino Nano 33 BLE | nRF52840, 64 MHz Cortex-M4, 256 KB SRAM |
| IMU | MPU-6050 breakout | I2C, A4=SDA, A5=SCL, INT → D2 |
| GSM module | SIM800L | SoftwareSerial D3/D4, RST → D5 |
| IMU power | 3.3 V from Arduino | |
| GSM power | Separate 3.7–4.2 V LiPo | Must supply 2 A peak |
| LED (status) | D13 built-in | Slow blink = idle, fast blink = fall candidate |
| LED (alert) | D7 | Lights on confirmed fall, clears after 5 s |

---

## Flashing the firmware

Install the following in the Arduino IDE or arduino-cli:

- Board: **Arduino Nano 33 BLE** (Arduino Mbed OS Nano Boards package)
- Library: **Arduino_TensorFlowLite** ≥ 2.4.0
- Library: **Wire** (built-in)

Before flashing, edit `ALERT_NUMBER` in `main.ino` to the target phone number.

To flash with arduino-cli:

```bash
arduino-cli compile --fqbn arduino:mbed_nano:nano33ble firmware/src
arduino-cli upload  --fqbn arduino:mbed_nano:nano33ble --port /dev/ttyACM0 firmware/src
```

---

## Training the classifier

Download one or both datasets:

- **SisFall**: http://sistemic.udea.edu.co/en/research/projects/english-falls/
- **MobiAct**: https://bmi.hmu.gr/the-mobifall-and-mobiact-datasets-2/

Place them under `ml/data/`:

```
ml/data/SisFall/SA01/D01_SA01_R01.txt ...
ml/data/MobiAct/falls/FOL_sub1_trial1.csv ...
```

Install dependencies and train:

```bash
pip install -r requirements.txt
cd ml/training
python train.py --augment --epochs 40
python export_model.py
```

`export_model.py` writes the updated `firmware/include/model_data.h`.
Recompile and flash the firmware to deploy the new model.

---

## Running the host monitor

```bash
pip install -r requirements.txt
python host/monitor.py --port /dev/ttyUSB0
python host/monitor.py --port /dev/ttyUSB0 --capture trace.csv --plot
```

The monitor colour-codes state transitions (yellow = fall candidate,
red = confirmed fall), optionally plots live SMV and saves raw IMU data to CSV
for offline analysis and dataset augmentation.

---

## Running the tests

```bash
pytest tests/test_fall_detect.py -v
```

27 tests covering SMV computation, free-fall detection, impact gating, FSM
state transitions (including timeout and cooldown), posture check suppression
of false positives, classifier confidence thresholds, circular window fill and
wrap-around, per-axis normalisation correctness and multiple consecutive fall
counting.

---

## Extending the project

**Deep sleep between samples** — the nRF52840 RTC can wake the processor at
100 Hz while keeping the core in sleep. This reduces idle current from ~20 mA
to < 2 mA, extending battery life from ~48 hours to several weeks.

**BLE alert fallback** — if the GSM module has no signal, the alert could be
sent as a BLE advertisement to a nearby smartphone. The Nano 33 BLE has the
hardware; it requires an additional BLE notification handler in the sketch.

**Larger model** — if RAM allows (256 KB on the nRF52840), a deeper model with
a third Conv1D layer and attention pooling can push sensitivity above 97% on
the SisFall benchmark with only ~3× inference cost.
