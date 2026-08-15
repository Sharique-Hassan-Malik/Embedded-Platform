#ifndef FALL_DETECT_H
#define FALL_DETECT_H

#include <stdint.h>
#include <stdbool.h>
#include "mpu6050.h"

/*
 * Fall detection pipeline.
 *
 * Stage 1 — Threshold pre-filter (runs every sample at 100 Hz)
 * ─────────────────────────────────────────────────────────────
 * Computes the signal magnitude vector (SMV) of the accelerometer:
 *
 *   SMV = sqrt(ax² + ay² + az²)
 *
 * A fall candidate is triggered when SMV drops below FREE_FALL_THRESHOLD
 * (free-fall phase: ~0.3–0.5 g) followed within IMPACT_WINDOW_MS by SMV
 * exceeding IMPACT_THRESHOLD (impact phase: >3 g).  This two-stage gate
 * eliminates the vast majority of normal activity without running the
 * classifier, keeping average power consumption low.
 *
 * Stage 2 — TFLite Micro classifier (runs only on fall candidates)
 * ────────────────────────────────────────────────────────────────
 * A 50-sample window (500 ms at 100 Hz) of 6-axis IMU data centred on the
 * impact instant is fed to a 1D-CNN classifier converted to a TFLite Micro
 * model and stored as a C byte array.
 *
 * Input tensor:  [1, 50, 6]  — float32
 *   Features:    ax, ay, az (g), gx, gy, gz (°/s)
 *   Normalised to mean 0, std 1 using training-set statistics.
 *
 * Output tensor: [1, 2]  — float32 softmax probabilities
 *   Index 0: not_fall probability
 *   Index 1: fall probability
 *
 * A fall alert is sent when fall_prob > FALL_CONF_THRESHOLD.
 *
 * Stage 3 — Post-fall posture check (optional)
 * ─────────────────────────────────────────────
 * After a positive classification, the system checks that the device
 * remains near-horizontal (|az| < LYING_THRESHOLD) for at least
 * LYING_DURATION_MS.  This reduces false positives from vigorous
 * activity that produces impact-like accelerations but is followed by
 * normal upright posture.
 */

/* Stage 1 thresholds. */
#define FREE_FALL_THRESHOLD   0.5f   /* g  — SMV below this triggers free-fall flag */
#define IMPACT_THRESHOLD      3.0f   /* g  — SMV above this triggers impact flag    */
#define IMPACT_WINDOW_MS      500u   /* ms — max gap between free-fall and impact   */
#define POST_IMPACT_MS        200u   /* ms — wait after impact before classifying   */

/* Stage 2. */
#define WINDOW_SAMPLES        50u    /* samples in classifier input window          */
#define WINDOW_AXES           6u     /* ax, ay, az, gx, gy, gz                      */
#define FALL_CONF_THRESHOLD   0.75f  /* minimum fall probability to trigger alert   */

/* Stage 3. */
#define LYING_THRESHOLD       0.5f   /* g  — |az| below this → lying flat           */
#define LYING_DURATION_MS     1000u  /* ms — how long device must stay flat         */

/* Alert cooldown — suppress repeated alerts for this many ms. */
#define ALERT_COOLDOWN_MS     30000u

/* Detector state. */
typedef enum {
    FD_IDLE        = 0,
    FD_FREE_FALL   = 1,   /* free-fall detected, waiting for impact  */
    FD_IMPACT      = 2,   /* impact detected, collecting window      */
    FD_CLASSIFYING = 3,   /* window full, running classifier         */
    FD_LYING       = 4,   /* classifier positive, checking posture   */
    FD_ALERT       = 5,   /* confirmed fall — alert in progress      */
    FD_COOLDOWN    = 6,   /* alert sent, suppressing repeats         */
} FdState;

typedef struct {
    FdState  state;
    uint32_t state_entry_ms;        /* millis() when state was entered             */
    uint32_t ff_entry_ms;           /* millis() when free-fall was detected        */

    /* Circular buffer: the last WINDOW_SAMPLES samples. */
    float    window[WINDOW_SAMPLES][WINDOW_AXES];
    uint8_t  win_head;              /* next write index                            */
    uint8_t  win_count;             /* samples currently in buffer (0–WINDOW_SAMPLES) */

    /* Classifier result. */
    float    fall_prob;
    float    nfall_prob;

    /* Counters. */
    uint32_t total_falls;
    uint32_t total_alerts;
    uint32_t false_positives_suppressed;  /* posture check rejected */
} FallDetector;

/*
 * Initialise the detector.  Must be called once before fd_update().
 */
void fd_init(FallDetector *fd);

/*
 * Process one IMU sample.
 * ms: current time in milliseconds (Arduino millis()).
 * Returns true if a confirmed fall is detected this call — the caller
 * should then send an SMS alert.
 */
bool fd_update(FallDetector *fd, const Mpu6050Sample *s, uint32_t ms);

/*
 * Run the TFLite Micro classifier on the current window.
 * Fills fd->fall_prob and fd->nfall_prob.
 * Returns true on successful inference.
 */
bool fd_classify(FallDetector *fd);

/*
 * Signal magnitude vector of the accelerometer.
 */
static inline float fd_smv(float ax, float ay, float az)
{
    return __builtin_sqrtf(ax * ax + ay * ay + az * az);
}

#endif /* FALL_DETECT_H */
