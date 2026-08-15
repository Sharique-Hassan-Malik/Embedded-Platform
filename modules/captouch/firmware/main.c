/*
 * PIC16F1829 Capacitive Touch Library — Demo Application
 *
 * Oscillator : 16 MHz internal HFINTOSC
 * Electrodes : 4 pads on RA0, RA1, RA2, RC0 (see touch_hw.c)
 *              Each pad has a 1 MΩ series resistor to GND.
 * UART output: RC4 at 9 600 8N1 → host terminal
 *
 * On startup the firmware runs a self-calibration with all electrodes clear,
 * then enters the 30 Hz scan loop.
 *
 * Every scan tick it emits a compact telemetry line:
 *   "D: +NNN +NNN +NNN +NNN  [T0 T1 T2 T3]\r\n"
 * where the four delta values are followed by the active flags in brackets.
 *
 * Whenever a gesture fires it prints a dedicated event line:
 *   "TAP 2\r\n"
 *   "HOLD 0\r\n"
 *   "SWIPE_LEFT 1\r\n"
 *   "SWIPE_RIGHT 2\r\n"
 *
 * Loop timing: Timer0 in 8-bit mode with prescaler 1:256 is used to produce
 * the ~33 ms tick.  At Fosc/4 = 4 MHz and prescaler 1:256 the counter
 * increments at 15 625 Hz; an 8-bit overflow fires at 15 625 / 256 ≈ 61 Hz.
 * We use a software counter to divide further:
 *   61 Hz / 2 = ~30.5 Hz, close enough for gesture timing.
 */

#include <xc.h>
#include <stdint.h>

#include "touch.h"
#include "gestures.h"
#include "uart.h"

/* ---- Configuration bits (PIC16F1829) ------------------------------------- */
#pragma config FOSC   = INTOSC   /* internal oscillator                 */
#pragma config WDTE   = OFF
#pragma config PWRTE  = ON
#pragma config MCLRE  = OFF      /* MCLR disabled — RA3 as input        */
#pragma config CP     = OFF
#pragma config CPD    = OFF
#pragma config BOREN  = ON
#pragma config CLKOUTEN = OFF
#pragma config IESO   = OFF
#pragma config FCMEN  = OFF
#pragma config WRT    = OFF
#pragma config PLLEN  = OFF
#pragma config STVREN = ON
#pragma config BORV   = LO
#pragma config LVP    = OFF

/* ---- Timing -------------------------------------------------------------- */
#define _XTAL_FREQ  16000000UL

/*
 * Timer0 8-bit, prescaler 1:256, Fosc/4 source.
 * Overflow period: 256 × 256 / 4 000 000 Hz = 16.384 ms → ~61 Hz.
 * We divide by 2 in software to get ~30 Hz.
 */
static uint8_t half_tick;   /* toggles every TMR0 overflow → 30 Hz flag */

/* ---- UART helpers -------------------------------------------------------- */

static void print_status(void)
{
    uint8_t ch;

    uart_puts("D:");
    for (ch = 0u; ch < TOUCH_CH_COUNT; ch++) {
        uart_putc(' ');
        uart_puti16(touch_delta(ch));
    }
    uart_puts("  [");
    for (ch = 0u; ch < TOUCH_CH_COUNT; ch++)
        uart_putc(touch_active(ch) ? '1' : '0');
    uart_puts("]\r\n");
}

static void print_gesture(gesture_event_t ev)
{
    switch (ev.type) {
    case GESTURE_TAP:
        uart_puts("TAP ");
        uart_putu16(ev.channel);
        uart_puts("\r\n");
        break;
    case GESTURE_HOLD:
        uart_puts("HOLD ");
        uart_putu16(ev.channel);
        uart_puts("\r\n");
        break;
    case GESTURE_SWIPE_LEFT:
        uart_puts("SWIPE_LEFT ");
        uart_putu16(ev.channel);
        uart_puts("\r\n");
        break;
    case GESTURE_SWIPE_RIGHT:
        uart_puts("SWIPE_RIGHT ");
        uart_putu16(ev.channel);
        uart_puts("\r\n");
        break;
    default:
        break;
    }
}

/* ---- Startup calibration sequence ---------------------------------------- */

static void calibrate_with_prompt(void)
{
    uart_puts("\r\n--- PIC16 Capacitive Touch ---\r\n");
    uart_puts("Calibrating (keep electrodes clear)...");
    touch_calibrate();
    uart_puts(" done.\r\n\r\n");

    /* Print per-channel baselines for diagnostic use. */
    {
        uint8_t ch;
        uart_puts("Baselines: ");
        for (ch = 0u; ch < TOUCH_CH_COUNT; ch++) {
            uart_putu16(touch_state[ch].baseline);
            if (ch < TOUCH_CH_COUNT - 1u) uart_putc(' ');
        }
        uart_puts("\r\n\r\n");
    }
}

/* ---- Timer0 setup -------------------------------------------------------- */

static void timer0_init(void)
{
    OPTION_REGbits.TMR0CS  = 0u;   /* internal Fosc/4 */
    OPTION_REGbits.PSA     = 0u;   /* prescaler assigned to Timer0 */
    OPTION_REGbits.PS      = 0b111; /* 1:256 prescaler */
    INTCONbits.TMR0IE      = 1u;
    INTCONbits.TMR0IF      = 0u;
}

/* ---- ISR ----------------------------------------------------------------- */

void __interrupt() isr(void)
{
    if (INTCONbits.TMR0IF) {
        INTCONbits.TMR0IF = 0u;
        half_tick++;
    }
}

/* ---- Main ---------------------------------------------------------------- */

int main(void)
{
    gesture_event_t ev;
    uint8_t         last_half;

    /* 16 MHz internal oscillator: IRCF = 1111. */
    OSCCONbits.IRCF  = 0b1111;
    OSCCONbits.SCS   = 0b10;   /* use INTOSC */

    uart_init();
    touch_init();
    gestures_init();
    calibrate_with_prompt();

    timer0_init();
    INTCONbits.GIE = 1u;   /* global interrupt enable */

    last_half = half_tick;

    for (;;) {
        /* Wait for the next half-tick (Timer0 overflow). */
        while (half_tick == last_half)
            ;
        last_half = half_tick;

        /* Divide by 2: act only on even half-ticks → ~30 Hz. */
        if (half_tick & 0x01u)
            continue;

        touch_scan();
        gestures_update();

        print_status();

        /* Drain and print all pending gesture events. */
        for (;;) {
            ev = gestures_get();
            if (ev.type == GESTURE_NONE) break;
            print_gesture(ev);
        }
    }

    return 0;
}
