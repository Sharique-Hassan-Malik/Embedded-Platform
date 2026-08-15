#include "touch.h"

#include <xc.h>
#include <stdint.h>

touch_ch_t touch_state[TOUCH_CH_COUNT];

/* ---- Timer1 helpers ------------------------------------------------------- */

/*
 * Timer1 is configured as a free-running 16-bit counter clocked by Fosc/4.
 * At 16 MHz: Fosc/4 = 4 MHz → 0.25 µs per tick.
 * Maximum count before overflow: 65 535 × 0.25 µs = 16.4 ms.
 * A typical no-touch charge time on a 2 cm² pad with 1 MΩ is 200–800 ticks.
 * A finger adds 10–50 pF, extending the count by 50–300 ticks at 16 MHz.
 * Counts are well within the 16-bit range.
 */
static void timer1_init(void)
{
    T1CON   = 0x00u;          /* stop timer, Fosc/4, prescaler 1:1 */
    T1GCON  = 0x00u;          /* gate off                          */
    TMR1H   = 0x00u;
    TMR1L   = 0x00u;
    T1CONbits.TMR1CS = 0u;   /* internal clock Fosc/4             */
    T1CONbits.T1CKPS = 0u;   /* 1:1 prescaler                     */
    /* Timer1 is started / stopped per measurement; not left running. */
}

/* Reset and start Timer1. */
static inline void timer1_start(void)
{
    TMR1H          = 0x00u;
    TMR1L          = 0x00u;
    T1CONbits.TMR1ON = 1u;
}

/* Stop Timer1 and return the 16-bit count. */
static inline uint16_t timer1_stop(void)
{
    T1CONbits.TMR1ON = 0u;
    /* Read high byte first then low (atomic snapshot). */
    return ((uint16_t)TMR1H << 8) | (uint16_t)TMR1L;
}

/* ---- Single-channel measurement ------------------------------------------ */

/*
 * Measure the charge time for one channel.
 *
 * Steps:
 *   1. Discharge: drive LOW to bleed any residual charge.
 *   2. Charge:   drive HIGH for a fixed pre-charge time.
 *   3. Release:  switch to input (hi-Z) and start Timer1.
 *   4. Wait:     poll until the pin reads LOW (discharged through R to GND).
 *   5. Record:   return the Timer1 count.
 *
 * A guard of TOUCH_COUNT_MAX prevents an infinite loop if the resistor is
 * missing or the pin is held high externally.
 *
 * Interrupts are disabled during the measurement to avoid Timer1 jitter from
 * ISR latency.  Each measurement takes < 50 µs at 16 MHz.
 */
static uint16_t measure_channel(uint8_t ch)
{
    const touch_pin_t *p = &TOUCH_PINS[ch];
    uint16_t           count;
    uint8_t            saved_gie;

    /* Disable analogue function (make pin digital) — idempotent after init. */
    *p->ansel &= ~p->mask;

    /* ---- Discharge phase ---- */
    *p->lat  &= ~p->mask;   /* drive LOW  */
    *p->tris &= ~p->mask;   /* set output */
    NOP(); NOP(); NOP(); NOP();   /* ~1 µs at 16 MHz — enough to fully discharge */
    NOP(); NOP(); NOP(); NOP();
    NOP(); NOP(); NOP(); NOP();
    NOP(); NOP(); NOP(); NOP();

    /* ---- Charge phase ---- */
    *p->lat  |= p->mask;    /* drive HIGH */
    NOP(); NOP(); NOP(); NOP();   /* brief pre-charge before releasing pin */
    NOP(); NOP(); NOP(); NOP();

    /* ---- Release and time ---- */
    saved_gie       = INTCONbits.GIE;
    INTCONbits.GIE  = 0u;

    *p->tris |= p->mask;    /* switch to input (hi-Z) — capacitor now discharges */
    timer1_start();

    /* Poll until discharge completes or guard count expires. */
    while ((*p->port & p->mask) && (TMR1H < (TOUCH_COUNT_MAX >> 8)))
        ;

    count = timer1_stop();

    INTCONbits.GIE = saved_gie;

    /* Leave pin as input with pin driver low — safe idle state. */
    *p->lat &= ~p->mask;

    return (count < TOUCH_COUNT_MAX) ? count : TOUCH_COUNT_MAX;
}

/* ---- Oversampled measurement --------------------------------------------- */

static uint16_t measure_oversampled(uint8_t ch)
{
    uint32_t sum = 0u;
    uint8_t  i;

    for (i = 0u; i < TOUCH_OVERSAMPLE; i++)
        sum += measure_channel(ch);

    return (uint16_t)(sum / TOUCH_OVERSAMPLE);
}

/* ---- Public API ----------------------------------------------------------- */

void touch_init(void)
{
    uint8_t ch;

    timer1_init();

    for (ch = 0u; ch < TOUCH_CH_COUNT; ch++) {
        const touch_pin_t *p = &TOUCH_PINS[ch];

        /* Disable analogue, set as output low (safe idle). */
        *p->ansel &= ~p->mask;
        *p->lat   &= ~p->mask;
        *p->tris  &= ~p->mask;

        touch_state[ch].threshold = TOUCH_DEFAULT_THR;
        touch_state[ch].baseline  = 0u;
        touch_state[ch].raw       = 0u;
        touch_state[ch].delta     = 0;
        touch_state[ch].active    = 0u;
    }
}

void touch_calibrate(void)
{
    uint8_t  ch, i;
    uint32_t acc;

    /* Average 16 measurements per channel for a stable initial baseline. */
    for (ch = 0u; ch < TOUCH_CH_COUNT; ch++) {
        acc = 0u;
        for (i = 0u; i < 16u; i++)
            acc += measure_channel(ch);
        touch_state[ch].baseline = (uint16_t)(acc >> 4);  /* divide by 16 */
        touch_state[ch].raw      = touch_state[ch].baseline;
        touch_state[ch].delta    = 0;
        touch_state[ch].active   = 0u;
    }
}

void touch_scan(void)
{
    uint8_t     ch;
    touch_ch_t *s;
    int16_t     delta;
    int32_t     bl_update;

    for (ch = 0u; ch < TOUCH_CH_COUNT; ch++) {
        s = &touch_state[ch];

        s->raw   = measure_oversampled(ch);
        delta    = (int16_t)s->raw - (int16_t)s->baseline;
        s->delta = delta;

        /* Hysteresis: activate on rising threshold, release on falling. */
        if (!s->active && delta > (int16_t)s->threshold) {
            s->active = 1u;
        } else if (s->active && delta < (int16_t)(s->threshold - TOUCH_HYST)) {
            s->active = 0u;
        }

        /*
         * Baseline update: only track drift when the electrode is not touched.
         * A touched electrode would pull the baseline upward and erode the delta.
         *
         * Update only in the positive direction (noise floor rising) when active,
         * allowing the baseline to track slow environmental drift even during
         * prolonged contact — but advance it only by 1 count/scan to avoid
         * training on a held touch.
         */
        if (!s->active) {
            bl_update = (int32_t)s->raw - (int32_t)s->baseline;
            s->baseline = (uint16_t)((int32_t)s->baseline
                          + (bl_update >> (int32_t)TOUCH_BASELINE_SHIFT));
        } else if (s->baseline > 0u && delta < 0) {
            /* Environmental drop while touched — still track downward. */
            s->baseline--;
        }
    }
}

int16_t touch_delta(uint8_t ch)
{
    return touch_state[ch].delta;
}

uint8_t touch_active(uint8_t ch)
{
    return touch_state[ch].active;
}

void touch_set_threshold(uint8_t ch, uint16_t thr)
{
    touch_state[ch].threshold = thr;
}
