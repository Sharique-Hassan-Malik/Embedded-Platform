#include "foc.h"
#include "foc_math.h"
#include "pid.h"
#include "hal.h"

#include <string.h>
#include <math.h>

/* ── Constants ──────────────────────────────────────────────────────────── */

#define CURRENT_LOOP_TS   (1.0f / 16000.0f)
#define SPEED_LOOP_TS     (1.0f / 1000.0f)
#define POS_LOOP_TS       (1.0f / 500.0f)

/* Alignment pulse duration: 200 ms at 16 kHz ISR rate. */
#define ALIGN_TICKS       3200u

/* Open-loop exit condition: reached minimum speed and synced to encoder. */
#define OL_EXIT_OMEGA     (10.0f * 2.0f * 3.14159f)   /* 10 rev/s */

/* ── Quarter-wave Q15 sine table ────────────────────────────────────────── */

const q15_t SINE_LUT[256] = {
        0,   201,   402,   603,   804,  1005,  1206,  1407,
     1608,  1809,  2009,  2210,  2410,  2611,  2811,  3012,
     3212,  3412,  3612,  3811,  4011,  4210,  4410,  4609,
     4808,  5007,  5205,  5404,  5602,  5800,  5998,  6195,
     6393,  6590,  6786,  6983,  7179,  7375,  7571,  7767,
     7962,  8157,  8351,  8545,  8739,  8933,  9126,  9319,
     9512,  9704,  9896, 10087, 10278, 10469, 10659, 10849,
    11039, 11228, 11417, 11605, 11793, 11980, 12167, 12353,
    12539, 12725, 12910, 13094, 13279, 13462, 13645, 13828,
    14010, 14191, 14372, 14553, 14732, 14912, 15090, 15269,
    15446, 15623, 15800, 15976, 16151, 16325, 16499, 16673,
    16846, 17018, 17189, 17360, 17530, 17700, 17869, 18037,
    18204, 18371, 18537, 18703, 18868, 19032, 19195, 19357,
    19519, 19680, 19841, 20000, 20159, 20317, 20475, 20631,
    20787, 20942, 21096, 21250, 21403, 21554, 21705, 21856,
    22005, 22154, 22301, 22448, 22594, 22739, 22884, 23027,
    23170, 23311, 23452, 23592, 23731, 23870, 24007, 24143,
    24279, 24413, 24547, 24680, 24811, 24942, 25072, 25201,
    25329, 25456, 25582, 25708, 25832, 25955, 26077, 26198,
    26319, 26438, 26556, 26674, 26790, 26905, 27019, 27133,
    27245, 27356, 27466, 27575, 27683, 27790, 27896, 28001,
    28105, 28208, 28310, 28411, 28510, 28609, 28706, 28803,
    28898, 28992, 29085, 29177, 29268, 29358, 29447, 29534,
    29621, 29706, 29791, 29874, 29956, 30037, 30117, 30195,
    30273, 30349, 30424, 30498, 30571, 30643, 30714, 30783,
    30852, 30919, 30985, 31050, 31113, 31176, 31237, 31297,
    31356, 31414, 31470, 31526, 31580, 31633, 31685, 31736,
    31785, 31833, 31880, 31926, 31971, 32014, 32057, 32098,
    32137, 32176, 32213, 32250, 32285, 32318, 32351, 32382,
    32412, 32441, 32469, 32495, 32521, 32545, 32567, 32589,
    32609, 32628, 32646, 32663, 32678, 32692, 32705, 32717,
    32728, 32737, 32745, 32752, 32757, 32761, 32765, 32766,
};

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static inline float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline float wrapf(float angle)
{
    while (angle >  3.14159265f) angle -= 6.28318530f;
    while (angle < -3.14159265f) angle += 6.28318530f;
    return angle;
}

/* ── Initialisation ──────────────────────────────────────────────────────── */

void foc_init(FocController *foc,
              uint8_t  pole_pairs,
              float    ls,
              float    rs,
              float    rated_current,
              float    encoder_cpr,
              float    vbus)
{
    memset(foc, 0, sizeof(*foc));

    foc->pole_pairs    = pole_pairs;
    foc->ls            = ls;
    foc->rs            = rs;
    foc->rated_current = rated_current;
    foc->encoder_cpr   = encoder_cpr;
    foc->vbus          = vbus;
    foc->mode          = FOC_MODE_IDLE;
}

void foc_set_current_gains(FocController *foc,
                           float kp, float ki, float kd, float kb)
{
    float lim = foc->vbus > 0.0f ? 0.95f : 1.0f;
    pid_init(&foc->pid_id, kp, ki, kd, kb, lim, lim);
    pid_init(&foc->pid_iq, kp, ki, kd, kb, lim, lim);
}

void foc_set_speed_gains(FocController *foc,
                         float kp, float ki, float kd, float kb)
{
    pid_init(&foc->pid_speed, kp, ki, kd, kb,
             foc->rated_current, foc->rated_current);
}

void foc_set_pos_gains(FocController *foc,
                       float kp, float ki, float kd, float kb)
{
    /* Speed limit for the position-to-speed output: 10 × rated mechanical speed. */
    float spd_lim = 100.0f * 3.14159f;
    pid_init(&foc->pid_pos, kp, ki, kd, kb, spd_lim, spd_lim);
}

/* ── Mode transitions ────────────────────────────────────────────────────── */

int foc_enable(FocController *foc)
{
    if (foc->mode != FOC_MODE_IDLE) return -1;
    if (foc->fault_flags)          return -1;

    pid_reset(&foc->pid_id);
    pid_reset(&foc->pid_iq);
    pid_reset(&foc->pid_speed);
    pid_reset(&foc->pid_pos);

    foc->ol_theta    = 0.0f;
    foc->ol_omega    = 0.0f;
    foc->ol_vd       = 0.20f;
    foc->ol_accel    = 20.0f * 3.14159f;   /* 10 rev/s² */
    foc->align_ticks = ALIGN_TICKS;
    foc->align_id    = 0.5f;

    foc->mode = FOC_MODE_ALIGN;
    hal_pwm_enable();
    return 0;
}

void foc_disable(FocController *foc)
{
    hal_pwm_disable();
    hal_pwm_set(0.0f, 0.0f, 0.0f);
    foc->mode = FOC_MODE_IDLE;
}

void foc_set_speed(FocController *foc, float omega_ref)
{
    foc->omega_ref = clampf(omega_ref, -300.0f * 3.14159f, 300.0f * 3.14159f);
    if (foc->mode == FOC_MODE_CURRENT)
        foc->mode = FOC_MODE_SPEED;
}

void foc_set_position(FocController *foc, float theta_ref)
{
    foc->theta_ref = theta_ref;
    if (foc->mode == FOC_MODE_SPEED || foc->mode == FOC_MODE_CURRENT)
        foc->mode = FOC_MODE_POSITION;
}

void foc_set_current(FocController *foc, float id_ref, float iq_ref)
{
    foc->id_ref = clampf(id_ref, -foc->rated_current, foc->rated_current);
    foc->iq_ref = clampf(iq_ref, -foc->rated_current, foc->rated_current);
}

void foc_inject_fault(FocController *foc, uint32_t mask)
{
    foc->fault_flags |= mask;
    if (foc->fault_flags) {
        foc_disable(foc);
        foc->mode = FOC_MODE_FAULT;
    }
}

void foc_clear_faults(FocController *foc)
{
    foc->fault_flags = 0;
    foc->mode        = FOC_MODE_IDLE;
}

/* ── Current ISR (16 kHz) ────────────────────────────────────────────────── */

void foc_current_isr(FocController *foc)
{
    foc->tick++;

    /* ── Over-current protection ──────────────────────────────────── */
    if (hal_oc_tripped()) {
        foc_inject_fault(foc, FOC_FAULT_OVERCURRENT);
        return;
    }

    /* ── State machine ────────────────────────────────────────────── */
    switch (foc->mode) {

    case FOC_MODE_IDLE:
    case FOC_MODE_FAULT:
        hal_pwm_set(0.5f, 0.5f, 0.5f);
        return;

    case FOC_MODE_ALIGN: {
        /* Drive Id = align_id, Iq = 0, theta = 0 to lock rotor to d-axis. */
        foc->vd = pid_update(&foc->pid_id, foc->align_id, foc->id, 0.0f);
        foc->vq = 0.0f;

        float valpha, vbeta;
        ipark_f(foc->vd, foc->vq, 0.0f, &valpha, &vbeta);
        svpwm_f(valpha, vbeta, &foc->ta, &foc->tb, &foc->tc);
        hal_pwm_set(foc->ta, foc->tb, foc->tc);

        if (foc->align_ticks > 0)
            foc->align_ticks--;
        else {
            foc->theta_mech = 0.0f;
            foc->ol_theta   = 0.0f;
            foc->mode       = FOC_MODE_OPENLOOP;
        }
        return;
    }

    case FOC_MODE_OPENLOOP: {
        /* V/f open-loop ramp until OL_EXIT_OMEGA is reached. */
        foc->ol_omega += foc->ol_accel * CURRENT_LOOP_TS;
        if (foc->ol_omega > OL_EXIT_OMEGA)
            foc->ol_omega = OL_EXIT_OMEGA;

        foc->ol_theta += foc->ol_omega * (float)foc->pole_pairs * CURRENT_LOOP_TS;
        if (foc->ol_theta >  3.14159265f) foc->ol_theta -= 6.28318530f;

        float s = sinf(foc->ol_theta);
        float c = cosf(foc->ol_theta);
        float valpha, vbeta;
        ipark_f(foc->ol_vd, 0.0f, foc->ol_theta, &valpha, &vbeta);
        svpwm_f(valpha, vbeta, &foc->ta, &foc->tb, &foc->tc);
        hal_pwm_set(foc->ta, foc->tb, foc->tc);
        (void)s; (void)c;

        /* Transition to closed-loop current control once speed is sufficient. */
        if (foc->ol_omega >= OL_EXIT_OMEGA) {
            pid_reset(&foc->pid_id);
            pid_reset(&foc->pid_iq);
            foc->mode = FOC_MODE_CURRENT;
        }
        return;
    }

    case FOC_MODE_CURRENT:
    case FOC_MODE_SPEED:
    case FOC_MODE_POSITION:
        break;   /* fall through to closed-loop control */
    }

    /* ── Closed-loop FOC ──────────────────────────────────────────── */

    /* 1. Clarke transform — ia, ib already populated by the caller. */
    clarke_f(foc->ia, foc->ib, &foc->alpha, &foc->beta);

    /* 2. Electrical angle from encoder. */
    float theta_elec = foc->theta_mech * (float)foc->pole_pairs;
    theta_elec = wrapf(theta_elec);

    /* 3. Park transform. */
    park_f(foc->alpha, foc->beta, theta_elec, &foc->id, &foc->iq);

    /* 4. Feed-forward decoupling voltages (cross-coupling compensation).
     *    Vd_ff = -omega_e * Ls * Iq
     *    Vq_ff =  omega_e * Ls * Id  +  omega_e * flux_linkage (ignored for SPMSM)
     */
    float omega_e = foc->omega_mech * (float)foc->pole_pairs;
    float vd_ff   = -omega_e * foc->ls * foc->iq;
    float vq_ff   =  omega_e * foc->ls * foc->id;

    /* 5. Current PI controllers. */
    foc->vd = pid_update(&foc->pid_id, foc->id_ref, foc->id, vd_ff);
    foc->vq = pid_update(&foc->pid_iq, foc->iq_ref, foc->iq, vq_ff);

    /* 6. Inverse Park. */
    ipark_f(foc->vd, foc->vq, theta_elec, &foc->valpha, &foc->vbeta);

    /* 7. SVPWM. */
    svpwm_f(foc->valpha, foc->vbeta, &foc->ta, &foc->tb, &foc->tc);

    /* 8. Write to PWM hardware. */
    hal_pwm_set(foc->ta, foc->tb, foc->tc);
}

/* ── Speed loop (1 kHz) ──────────────────────────────────────────────────── */

void foc_speed_update(FocController *foc)
{
    if (foc->mode < FOC_MODE_SPEED) return;

    foc->iq_ref = pid_update(&foc->pid_speed,
                             foc->omega_ref, foc->omega_mech, 0.0f);
    foc->iq_ref = clampf(foc->iq_ref, -foc->rated_current, foc->rated_current);
}

/* ── Position loop (500 Hz) ──────────────────────────────────────────────── */

void foc_position_update(FocController *foc)
{
    if (foc->mode < FOC_MODE_POSITION) return;

    foc->omega_ref = pid_update(&foc->pid_pos,
                                foc->theta_ref, foc->theta_mech, 0.0f);
}
