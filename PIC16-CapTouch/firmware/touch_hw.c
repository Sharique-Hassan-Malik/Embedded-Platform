#include "touch_hw.h"

/*
 * touch_hw.c — Concrete pin assignments for the PIC16F1829 demo board.
 *
 * Edit the TOUCH_PINS table to remap channels to different pins.
 * Adding or removing channels requires adjusting TOUCH_CH_COUNT in touch_hw.h
 * and re-running touch_calibrate().
 */

const touch_pin_t TOUCH_PINS[TOUCH_CH_COUNT] = {
    /* ch 0 — RA0 */ { &PORTA, &LATA, &TRISA, &ANSELA, 0x01u },
    /* ch 1 — RA1 */ { &PORTA, &LATA, &TRISA, &ANSELA, 0x02u },
    /* ch 2 — RA2 */ { &PORTA, &LATA, &TRISA, &ANSELA, 0x04u },
    /* ch 3 — RC0 */ { &PORTC, &LATC, &TRISC, &ANSELC, 0x01u },
};
