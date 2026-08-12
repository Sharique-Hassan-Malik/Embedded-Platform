#include "buttons.h"

#include <xc.h>

/* Number of consecutive identical samples required before a state change
 * is accepted.  At 30 Hz a count of 3 gives 100 ms debounce time. */
#define DEBOUNCE_COUNT  3u

static uint8_t held[BTN_COUNT];      /* current debounced state (1 = pressed) */
static uint8_t counter[BTN_COUNT];   /* consecutive sample counter             */
static uint8_t press_flag[BTN_COUNT];/* pending edge flags                     */

void buttons_init(void)
{
    uint8_t i;

    /* RB0–RB3 as digital inputs. */
    TRISBbits.TRISB0 = 1;
    TRISBbits.TRISB1 = 1;
    TRISBbits.TRISB2 = 1;
    TRISBbits.TRISB3 = 1;

    /* RB0–RB3 as digital: the PIC18F4550 has no per-pin ANSEL registers;
       analogue inputs are disabled globally via ADCON1 (= 0x0F) in
       system_init(). */

    /* Enable PORTB weak pull-ups. */
    INTCON2bits.nRBPU = 0;

    for (i = 0; i < BTN_COUNT; i++) {
        held[i]       = 0;
        counter[i]    = 0;
        press_flag[i] = 0;
    }
}

void buttons_update(void)
{
    uint8_t portb = PORTB;
    uint8_t i, raw;

    for (i = 0; i < BTN_COUNT; i++) {
        /* Active-low: bit = 0 means the button is physically pressed. */
        raw = ((portb >> i) & 0x01u) ? 0u : 1u;

        if (raw == held[i]) {
            counter[i] = 0;
        } else {
            counter[i]++;
            if (counter[i] >= DEBOUNCE_COUNT) {
                held[i]    = raw;
                counter[i] = 0;
                if (raw)                  /* 0→1 transition = new press */
                    press_flag[i] = 1;
            }
        }
    }
}

uint8_t btn_held(button_t b)
{
    return held[(uint8_t)b];
}

uint8_t btn_pressed(button_t b)
{
    uint8_t flag = press_flag[(uint8_t)b];
    press_flag[(uint8_t)b] = 0;
    return flag;
}
