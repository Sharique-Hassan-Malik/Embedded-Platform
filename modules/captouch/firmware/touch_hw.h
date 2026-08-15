#ifndef TOUCH_HW_H
#define TOUCH_HW_H

/*
 * touch_hw.h — Board-level pin assignment for the capacitive touch library.
 *
 * Target: PIC16F1829 at 16 MHz (internal HFINTOSC).
 * Edit only this file when porting the library to a different PIC16/PIC18
 * device or board layout.
 *
 * Each channel is described by three register addresses and a single-bit mask:
 *   PORT  — the PORTx register (read back pin state)
 *   LAT   — the LATx  register (drive output level)
 *   TRIS  — the TRISx register (direction: 0 = output, 1 = input)
 *   ANSEL — the ANSELx register (must clear the bit to make the pin digital)
 *   mask  — bitmask for the single pin within the above registers
 *
 * Electrode geometry assumed in this demo:
 *   CH0  RA0  — leftmost pad
 *   CH1  RA1
 *   CH2  RA2
 *   CH3  RC0  — rightmost pad
 *
 * Each electrode uses a 1 MΩ series resistor to ground as the discharge path.
 * Pad size and series resistor value are the primary tuning parameters;
 * see docs/ARCHITECTURE.md for the full tuning methodology.
 *
 * TOUCH_CH_COUNT must equal the number of initialised entries in TOUCH_PINS[].
 */

#include <xc.h>
#include <stdint.h>

#define TOUCH_CH_COUNT  4u

typedef struct {
    volatile uint8_t *port;
    volatile uint8_t *lat;
    volatile uint8_t *tris;
    volatile uint8_t *ansel;
    uint8_t           mask;
} touch_pin_t;

/*
 * Populated in touch_hw.c.
 * Declared extern so touch.c and gestures.c can read TOUCH_CH_COUNT without
 * a circular dependency on the concrete pin addresses.
 */
extern const touch_pin_t TOUCH_PINS[TOUCH_CH_COUNT];

#endif /* TOUCH_HW_H */
