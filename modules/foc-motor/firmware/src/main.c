#include "hal.h"
#include "foc.h"
#include "foc_math.h"

#include <string.h>
#include <math.h>
#include <stdbool.h>

/* ── Motor parameters (example: small BLDC, 4-pole, 0.5 mH, 0.3 Ω) ──── */
#define POLE_PAIRS      2u
#define PHASE_INDUCTANCE 0.0005f   /* H */
#define PHASE_RESISTANCE 0.3f      /* Ω */
#define RATED_CURRENT    8.0f      /* A peak */
#define NOMINAL_VBUS     24.0f     /* V */

/* ── Current loop PID gains (bandwidth ≈ Rs/Ls = 600 rad/s, tuned below)
 *    Kp = Ls * bandwidth = 0.0005 * 600 = 0.30
 *    Ki = Rs * bandwidth / (Ls * sampling_freq) — discretised
 */
#define I_KP   0.30f
#define I_KI   0.005f
#define I_KD   0.0f
#define I_KB   0.015f

/* ── Speed loop PID gains ──────────────────────────────────────────────── */
#define W_KP   0.05f
#define W_KI   0.002f
#define W_KD   0.0f
#define W_KB   0.002f

/* ── Position loop PID gains ───────────────────────────────────────────── */
#define P_KP   8.0f
#define P_KI   0.1f
#define P_KD   0.0f
#define P_KB   0.1f

/* ── Global controller instance ────────────────────────────────────────── */
static FocController g_foc;

/* ── Encoder velocity estimator state ──────────────────────────────────── */
static int32_t prev_enc;
static float   omega_filt;

/* ── Speed loop divider (current ISR at 16 kHz, speed at 1 kHz) ─────────
 * The 1 kHz TIM2 interrupt calls foc_speed_callback() directly.
 * The position loop runs every other speed tick (500 Hz).
 */
static uint32_t pos_tick;

/* ── Telemetry rate limiter: send one frame every 16 current ISR ticks
 * (1 kHz telemetry rate). ─────────────────────────────────────────────── */
static uint32_t telem_div;

/* ════════════════════════════════════════════════════════════════════════ */
/*  ADC callback — runs from ADC_IRQHandler at 16 kHz                     */
/* ════════════════════════════════════════════════════════════════════════ */
void foc_adc_callback(void)
{
    /* 1. Read raw ADC. */
    uint16_t ia_raw, ib_raw;
    hal_adc_read(&ia_raw, &ib_raw);

    /* 2. Convert to amperes (offset + scale). */
    g_foc.ia = ((float)ia_raw - CURRENT_OFFSET) * CURRENT_SCALE;
    g_foc.ib = ((float)ib_raw - CURRENT_OFFSET) * CURRENT_SCALE;

    /* 3. Read encoder and update mechanical angle. */
    int32_t enc = hal_encoder_read();
    g_foc.encoder_counts  = enc;
    g_foc.theta_mech      = ((float)enc / ENCODER_CPR) * 2.0f * 3.14159265f;

    /* 4. Run the inner current loop. */
    foc_current_isr(&g_foc);

    /* 5. Telemetry (non-blocking, dropped if DMA busy). */
    telem_div++;
    if (telem_div >= 16) {
        telem_div = 0;
        TelemFrame tf;
        uint8_t csum = 0;
        tf.magic  = TELEM_MAGIC;
        tf.tick   = g_foc.tick;
        tf.id     = g_foc.id;
        tf.iq     = g_foc.iq;
        tf.omega  = g_foc.omega_mech;
        tf.theta  = g_foc.theta_mech;
        tf.mode   = (uint8_t)g_foc.mode;
        tf.faults = (uint8_t)g_foc.fault_flags;
        const uint8_t *p = (const uint8_t *)&tf;
        for (int i = 0; i < (int)sizeof(tf) - 1; i++) csum ^= p[i];
        tf.csum = csum;
        hal_telem_send(&tf);
    }
}

/* ════════════════════════════════════════════════════════════════════════ */
/*  Speed/position callback — runs from TIM2_IRQHandler at 1 kHz          */
/* ════════════════════════════════════════════════════════════════════════ */
void foc_speed_callback(void)
{
    /* Velocity estimate: encoder delta over 1 ms, single-pole IIR filter. */
    int32_t enc    = hal_encoder_read();
    int32_t delta  = enc - prev_enc;
    prev_enc       = enc;

    float omega_raw = ((float)delta / ENCODER_CPR) * 2.0f * 3.14159265f * 1000.0f;
    omega_filt      = omega_filt + SPEED_ALPHA * (omega_raw - omega_filt);
    g_foc.omega_mech = omega_filt;

    foc_speed_update(&g_foc);

    pos_tick++;
    if (pos_tick >= 2) {
        pos_tick = 0;
        foc_position_update(&g_foc);
    }

    /* Blink LED at 2 Hz when running. */
    if ((g_foc.tick & 0x1FFF) == 0) hal_led_toggle();
}

/* ════════════════════════════════════════════════════════════════════════ */
/*  main                                                                   */
/* ════════════════════════════════════════════════════════════════════════ */
int main(void)
{
    hal_init();

    foc_init(&g_foc, POLE_PAIRS, PHASE_INDUCTANCE, PHASE_RESISTANCE,
             RATED_CURRENT, ENCODER_CPR, NOMINAL_VBUS);

    foc_set_current_gains(&g_foc, I_KP, I_KI, I_KD, I_KB);
    foc_set_speed_gains  (&g_foc, W_KP, W_KI, W_KD, W_KB);
    foc_set_pos_gains    (&g_foc, P_KP, P_KI, P_KD, P_KB);

    prev_enc   = 0;
    omega_filt = 0.0f;
    telem_div  = 0;
    pos_tick   = 0;

    hal_irq_enable();

    /* Start motor: align → open-loop ramp → closed-loop current.
     * Then request speed mode at 60 rev/s (≈ 377 rad/s). */
    foc_enable(&g_foc);
    hal_delay_us(500000);           /* 500 ms alignment + open-loop ramp */
    foc_set_speed(&g_foc, 60.0f * 2.0f * 3.14159265f);

    /* Main loop: supervise DC bus voltage, toggle LED on fault. */
    for (;;) {
        float vbus = hal_vbus_read();

        if (vbus < 18.0f) {
            foc_inject_fault(&g_foc, FOC_FAULT_UNDERVOLTAGE);
        } else if (vbus > 28.0f) {
            foc_inject_fault(&g_foc, FOC_FAULT_OVERVOLTAGE);
        }

        if (g_foc.mode == FOC_MODE_FAULT) {
            hal_led_set(true);
            hal_delay_us(100000);
            hal_led_set(false);
            hal_delay_us(100000);
        }

        hal_delay_us(1000);
    }
}
