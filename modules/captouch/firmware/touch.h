#ifndef TOUCH_H
#define TOUCH_H

#include <stdint.h>
#include "touch_hw.h"

/*
 * Capacitive touch library for PIC16 — charge-time measurement (CTM) engine.
 *
 * Measurement principle
 * ─────────────────────
 * Each sensing electrode is connected to a GPIO pin and a 1 MΩ discharge
 * resistor to ground (no other hardware required).  One measurement cycle:
 *
 *   1. Drive pin LOW (output) → fully discharge the electrode capacitance.
 *   2. Drive pin HIGH (output) → fully charge the electrode capacitance.
 *   3. Immediately switch pin to input (hi-Z); Timer1 is reset and started.
 *   4. Poll the pin.  The capacitor discharges through the series resistor.
 *      When the pin reads LOW Timer1 is stopped.
 *   5. Timer1 count ∝ R × C.  A finger on the electrode increases C by
 *      10–50 pF, increasing the charge time proportionally.
 *
 * Baseline and delta
 * ──────────────────
 * The no-touch count (baseline) drifts slowly with temperature and humidity.
 * An exponential moving average tracks the baseline when no touch is detected:
 *
 *   baseline += (raw - baseline) >> TOUCH_BASELINE_SHIFT
 *
 * The delta (raw − baseline) is compared against a threshold to detect touch.
 * A positive delta means a finger is present (higher capacitance = longer
 * charge time = higher count).
 *
 * All values are 16-bit unsigned.  Timer1 is 16-bit so raw counts fit.
 *
 * Call sequence
 * ─────────────
 *   touch_init()       — one-time hardware setup
 *   touch_calibrate()  — measure baseline (call with electrodes clear)
 *   loop:
 *     touch_scan()     — measure all channels, update state
 *     touch_delta(ch)  — read filtered delta for a channel
 *     touch_active(ch) — 1 if ch is currently touched
 */

/* Number of samples averaged per channel per scan (1–8).
 * Higher values reduce noise at the cost of scan time.
 * Typical: 4.  Change here only; no other file needs editing. */
#define TOUCH_OVERSAMPLE      4u

/* Baseline IIR filter shift.  Larger = slower adaptation.
 * 6 → τ ≈ 64 scans.  At 30 scans/s that is ~2 s — suitable for most pads. */
#define TOUCH_BASELINE_SHIFT  6u

/* Default touch threshold (raw counts above baseline).
 * Adjust per electrode geometry: larger pads need a higher threshold.
 * Can be overridden per channel with touch_set_threshold(). */
#define TOUCH_DEFAULT_THR     80u

/* Hysteresis counts.  A touched channel requires delta > threshold to
 * activate and delta < (threshold - TOUCH_HYST) to release.
 * Prevents chatter around the detection boundary. */
#define TOUCH_HYST            20u

/* Maximum plausible raw count.  Readings above this are discarded as noise
 * (e.g. pin floating, resistor disconnected). */
#define TOUCH_COUNT_MAX       60000u

/* Per-channel state. */
typedef struct {
    uint16_t baseline;    /* long-term no-touch average (IIR)     */
    uint16_t raw;         /* most recent oversampled measurement   */
    int16_t  delta;       /* raw − baseline                        */
    uint16_t threshold;   /* per-channel touch detection threshold */
    uint8_t  active;      /* 1 = currently touched, 0 = released   */
} touch_ch_t;

/* Initialise Timer1 and configure all electrode pins as digital outputs.
 * Does NOT measure baseline — call touch_calibrate() afterwards. */
void touch_init(void);

/* Measure baseline for all channels.
 * Call with all electrodes clear (nothing touching them).
 * Takes TOUCH_OVERSAMPLE × TOUCH_CH_COUNT measurements; blocks for ~5 ms. */
void touch_calibrate(void);

/* Measure all channels and update touch_state[].
 * Call at your desired scan rate (typically 10–100 Hz).
 * Blocks for the duration of all measurements (~0.5 ms at 16 MHz). */
void touch_scan(void);

/* Return the filtered delta (raw − baseline) for channel ch.
 * Positive values indicate a finger present. */
int16_t touch_delta(uint8_t ch);

/* Return 1 if channel ch is currently touched (with hysteresis). */
uint8_t touch_active(uint8_t ch);

/* Override the touch threshold for a specific channel (default TOUCH_DEFAULT_THR).
 * Call after touch_init() and before the scan loop. */
void touch_set_threshold(uint8_t ch, uint16_t thr);

/* Raw access to per-channel state (read-only from caller's perspective). */
extern touch_ch_t touch_state[TOUCH_CH_COUNT];

#endif /* TOUCH_H */
