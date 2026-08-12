#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdint.h>

/*
 * Debounced tactile button driver.
 *
 * Four buttons on PORTB, active low (pressed = 0, released = 1).
 * Internal weak pull-ups are enabled via INTCON2.nRBPU.
 *
 * Pin assignments:
 *   RB0 — BTN_LEFT
 *   RB1 — BTN_RIGHT
 *   RB2 — BTN_FIRE
 *   RB3 — BTN_EXTRA  (spare; unused in Breakout)
 */

typedef enum {
    BTN_LEFT  = 0,
    BTN_RIGHT = 1,
    BTN_FIRE  = 2,
    BTN_EXTRA = 3,
    BTN_COUNT = 4
} button_t;

/* Call once at startup. */
void buttons_init(void);

/*
 * Call once per game frame (30 Hz).
 * Samples PORTB and updates internal debounce counters.
 */
void buttons_update(void);

/*
 * Returns 1 if the button is currently held down (after debounce).
 * Returns 0 otherwise.
 */
uint8_t btn_held(button_t b);

/*
 * Returns 1 exactly once per press event (rising edge of held state).
 * The flag is cleared after reading.
 */
uint8_t btn_pressed(button_t b);

#endif /* BUTTONS_H */
