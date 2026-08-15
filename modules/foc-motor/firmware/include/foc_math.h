#ifndef FOC_MATH_H
#define FOC_MATH_H

#include <stdint.h>
#include <math.h>

/*
 * Fixed-point Q15 type.  All values in [-1, 1) are represented as
 * a 16-bit signed integer scaled by 2^15 = 32768.
 */
typedef int16_t q15_t;

#define Q15_ONE     ((q15_t)0x7FFF)
#define Q15(x)      ((q15_t)((x) * 32767.0f))
#define Q15_TO_F(x) ((float)(x) / 32768.0f)

/* Saturating multiply: (a * b) >> 15 with Q15 headroom. */
static inline q15_t q15_mul(q15_t a, q15_t b)
{
    return (q15_t)(((int32_t)a * (int32_t)b) >> 15);
}

/*
 * Clarke transform (alpha-beta): converts three-phase currents
 * ia, ib, ic to two-phase stationary frame (alpha, beta).
 *
 * Assumes ia + ib + ic = 0, so ic is not needed.
 *
 *   alpha = ia
 *   beta  = (ia + 2*ib) / sqrt(3)
 *
 * Float version used in the host simulator and test suite.
 */
static inline void clarke_f(float ia, float ib,
                             float *alpha, float *beta)
{
    *alpha = ia;
    *beta  = (ia + 2.0f * ib) * 0.57735026919f;  /* 1/sqrt(3) */
}

/*
 * Q15 Clarke transform.  Inputs are Q15; outputs are Q15.
 * The 1/sqrt(3) factor is pre-scaled to Q15: 0.57735 * 32767 ≈ 18919.
 */
static inline void clarke_q15(q15_t ia, q15_t ib,
                               q15_t *alpha, q15_t *beta)
{
    *alpha = ia;
    /* beta = (ia + 2*ib) * (18919 >> 15) using 32-bit intermediate */
    int32_t tmp = (int32_t)ia + 2 * (int32_t)ib;
    *beta = (q15_t)((tmp * 18919L) >> 15);
}

/*
 * Park transform: rotates the stationary (alpha, beta) frame to the
 * rotating (d, q) frame aligned with the rotor flux vector at angle theta.
 *
 *   d =  alpha * cos(theta) + beta * sin(theta)
 *   q = -alpha * sin(theta) + beta * cos(theta)
 */
static inline void park_f(float alpha, float beta, float theta,
                           float *d, float *q)
{
    float s = sinf(theta);
    float c = cosf(theta);
    *d =  alpha * c + beta * s;
    *q = -alpha * s + beta * c;
}

/*
 * Q15 Park transform.  sin_t and cos_t are the Q15 sin and cos of theta,
 * pre-computed by the caller from a lookup table to avoid trig in the ISR.
 */
static inline void park_q15(q15_t alpha, q15_t beta,
                             q15_t sin_t, q15_t cos_t,
                             q15_t *d, q15_t *q)
{
    *d = (q15_t)(((int32_t)alpha * cos_t + (int32_t)beta  * sin_t) >> 15);
    *q = (q15_t)(((int32_t)beta  * cos_t - (int32_t)alpha * sin_t) >> 15);
}

/*
 * Inverse Park transform: rotates (Vd, Vq) back to stationary (Valpha, Vbeta).
 *
 *   Valpha = Vd * cos(theta) - Vq * sin(theta)
 *   Vbeta  = Vd * sin(theta) + Vq * cos(theta)
 */
static inline void ipark_f(float vd, float vq, float theta,
                            float *valpha, float *vbeta)
{
    float s = sinf(theta);
    float c = cosf(theta);
    *valpha = vd * c - vq * s;
    *vbeta  = vd * s + vq * c;
}

static inline void ipark_q15(q15_t vd, q15_t vq,
                              q15_t sin_t, q15_t cos_t,
                              q15_t *valpha, q15_t *vbeta)
{
    *valpha = (q15_t)(((int32_t)vd * cos_t - (int32_t)vq * sin_t) >> 15);
    *vbeta  = (q15_t)(((int32_t)vd * sin_t + (int32_t)vq * cos_t) >> 15);
}

/*
 * Space Vector PWM (SVPWM).
 *
 * Inputs: Valpha, Vbeta in [-1, 1] (normalised to Vbus/2).
 * Outputs: ta, tb, tc — duty cycles in [0, 1] for phases A, B, C.
 *
 * Algorithm:
 *   1. Determine the active sector (1–6).
 *   2. Compute dwell times T1, T2 for the two active vectors.
 *   3. Distribute Tz (zero-vector time) symmetrically → centred SVPWM.
 *   4. Convert dwell fractions to per-phase compare values.
 *
 * Reference: Mohan, Undeland, Robbins — "Power Electronics", Appendix B.
 */
static inline void svpwm_f(float valpha, float vbeta,
                            float *ta, float *tb, float *tc)
{
    /* Intermediate phase voltages (un-normalised sector detection). */
    float v1 =  vbeta;
    float v2 = -vbeta * 0.5f + valpha * 0.8660254f;   /* sqrt(3)/2 */
    float v3 = -vbeta * 0.5f - valpha * 0.8660254f;

    int sector;
    if      (v1 > 0.0f && v2 >= 0.0f && v3 <  0.0f) sector = 1;
    else if (v1 > 0.0f && v2 <  0.0f && v3 <  0.0f) sector = 2;
    else if (v1 <= 0.0f && v2 <  0.0f && v3 <  0.0f) sector = 3;
    else if (v1 <  0.0f && v2 <  0.0f && v3 >= 0.0f) sector = 4;
    else if (v1 <  0.0f && v2 >= 0.0f && v3 >= 0.0f) sector = 5;
    else                                               sector = 6;

    /* T1, T2: active vector dwell fractions (both in [0, 1]). */
    float t1, t2;
    switch (sector) {
        case 1: t1 =  v2; t2 =  v1; break;
        case 2: t1 = -v3; t2 = -v2; break;
        case 3: t1 =  v1; t2 =  v3; break;
        case 4: t1 = -v2; t2 = -v1; break;
        case 5: t1 =  v3; t2 =  v2; break;
        default:t1 = -v1; t2 = -v3; break;
    }

    /* Clamp to unit triangle to prevent over-modulation. */
    float total = t1 + t2;
    if (total > 1.0f) { t1 /= total; t2 /= total; }

    float tz = (1.0f - t1 - t2) * 0.5f;   /* zero-vector half-time */

    /* Per-phase compare values — centred PWM. */
    float tA, tB, tC;
    switch (sector) {
        case 1: tA = tz + t1 + t2; tB = tz + t2;      tC = tz;           break;
        case 2: tA = tz + t1;      tB = tz + t1 + t2; tC = tz;           break;
        case 3: tA = tz;           tB = tz + t1 + t2; tC = tz + t2;      break;
        case 4: tA = tz;           tB = tz + t1;      tC = tz + t1 + t2; break;
        case 5: tA = tz + t2;      tB = tz;           tC = tz + t1 + t2; break;
        default:tA = tz + t1 + t2; tB = tz;           tC = tz + t1;      break;
    }

    *ta = tA; *tb = tB; *tc = tC;
}

/* ---- sin/cos lookup table (256 entries, quarter-wave, Q15) ------------- */

/*
 * 256-entry quarter-wave Q15 sine table.
 * Index maps [0..255] to angles [0, pi/2) in 512ths of a full turn.
 * Full sine lookup uses symmetry: sin_lut(angle & 0xFF, quadrant).
 */
extern const q15_t SINE_LUT[256];

/*
 * Evaluate sin(theta) in Q15 where theta is a 16-bit unsigned angle
 * representing [0, 2*pi) linearly (i.e. 0x0000 = 0, 0x8000 = pi).
 */
static inline q15_t sin_q15(uint16_t angle)
{
    uint8_t idx = (uint8_t)(angle >> 7);  /* top 9 bits → quarter + 7-bit index */
    uint8_t quad = angle >> 14;
    uint8_t i    = (uint8_t)(angle >> 6) & 0xFF;

    (void)idx;   /* suppress unused */
    q15_t s;
    switch (quad & 3) {
        case 0: s =  SINE_LUT[i];           break;
        case 1: s =  SINE_LUT[255 - i];     break;
        case 2: s = -SINE_LUT[i];           break;
        default:s = -SINE_LUT[255 - i];     break;
    }
    return s;
}

static inline q15_t cos_q15(uint16_t angle)
{
    return sin_q15((uint16_t)(angle + 0x4000));  /* cos(x) = sin(x + pi/2) */
}

#endif /* FOC_MATH_H */
