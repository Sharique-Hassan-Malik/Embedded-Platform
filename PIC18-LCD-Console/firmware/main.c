/*
 * Retro LCD Game Console — Breakout
 * Target : PIC18F4550, 8 MHz internal oscillator
 * Display: KS0108 128×64 monochrome LCD (parallel)
 * Input  : 3 tactile buttons (LEFT, RIGHT, FIRE) on RB0–RB2
 * Storage: PIC18F4550 internal data EEPROM (high score)
 *
 * Game loop: 30 Hz driven by Timer0 overflow interrupt.
 *   ISR sets tick_flag.
 *   Main loop detects tick_flag, calls buttons_update() + game_tick()
 *   + game_render() + lcd_flush().
 *
 * Timer0 setup for 30 Hz:
 *   Fosc  = 8 MHz → Fcy = 2 MHz (one instruction cycle = 500 ns)
 *   Timer0 16-bit mode, prescaler 1:8 → increments at 250 kHz
 *   Counts per period: 250 000 / 30 = 8 333
 *   Reload value: 65 536 – 8 333 = 57 203 = 0xDF53
 */

#include <xc.h>
#include <stdint.h>

#include "lcd.h"
#include "gfx.h"
#include "buttons.h"
#include "eeprom.h"
#include "game.h"

#define _XTAL_FREQ 8000000UL

/* ---- configuration bits -------------------------------------------------- */
/* CONFIG1L */
#pragma config PLLDIV  = 1          /* PLL prescaler ÷1 (not using USB)     */
#pragma config CPUDIV  = OSC1_PLL2  /* CPU clock = oscillator               */
#pragma config USBDIV  = 1          /* USB clock source (unused)            */
/* CONFIG1H */
#pragma config FOSC    = INTOSCIO_EC  /* Internal RC, I/O on RA6/RA7        */
#pragma config FCMEN   = OFF
#pragma config IESO    = OFF
/* CONFIG2L */
#pragma config PWRT    = ON         /* Power-up timer enabled               */
#pragma config BOR     = OFF        /* Brown-out reset disabled             */
#pragma config BORV    = 3
#pragma config VREGEN  = OFF
/* CONFIG2H */
#pragma config WDT     = OFF
#pragma config WDTPS   = 32768
/* CONFIG3H */
#pragma config CCP2MX  = ON
#pragma config PBADEN  = OFF        /* PORTB<4:0> as digital on reset       */
#pragma config LPT1OSC = OFF
#pragma config MCLRE   = ON
/* CONFIG4L */
#pragma config STVREN  = ON
#pragma config LVP     = OFF
#pragma config ICPRT   = OFF
#pragma config XINST   = OFF
/* Code protection: all off */
#pragma config CP0=OFF, CP1=OFF, CP2=OFF, CP3=OFF, CPB=OFF, CPD=OFF
#pragma config WRT0=OFF, WRT1=OFF, WRT2=OFF, WRT3=OFF
#pragma config WRTB=OFF, WRTC=OFF, WRTD=OFF
#pragma config EBTR0=OFF, EBTR1=OFF, EBTR2=OFF, EBTR3=OFF, EBTRB=OFF

/* ---- Timer0 reload value ------------------------------------------------- */
#define TMR0_RELOAD_H  0xDFu
#define TMR0_RELOAD_L  0x53u

/* ---- shared ISR flag ----------------------------------------------------- */
static volatile uint8_t tick_flag;

/* ---- Timer0 ISR (high priority) ------------------------------------------ */
void __interrupt(high_priority) high_isr(void)
{
    if (INTCONbits.TMR0IF) {
        /* Reload before clearing the flag to minimise drift. */
        TMR0H = TMR0_RELOAD_H;
        TMR0L = TMR0_RELOAD_L;
        INTCONbits.TMR0IF = 0;
        tick_flag = 1;
    }
}

/* ---- hardware initialisation --------------------------------------------- */
static void system_init(void)
{
    /* Internal oscillator at 8 MHz: IRCF<2:0> = 111. */
    OSCCON = 0x72u;

    /* Wait for oscillator to stabilise. */
    while (!OSCCONbits.IOFS)
        ;

    /* All analogue inputs off — make everything digital. */
    ADCON1 = 0x0Fu;
}

static void timer0_init(void)
{
    T0CON = 0;

    /* 16-bit mode, internal clock, prescaler 1:8. */
    T0CONbits.T08BIT  = 0;   /* 16-bit                */
    T0CONbits.T0CS    = 0;   /* internal (Fosc/4)     */
    T0CONbits.PSA     = 0;   /* prescaler assigned    */
    T0CONbits.T0PS    = 0b010; /* 1:8 prescaler        */

    /* Load initial count. */
    TMR0H = TMR0_RELOAD_H;
    TMR0L = TMR0_RELOAD_L;

    /* Clear flag, set high priority, enable Timer0 interrupt. */
    INTCONbits.TMR0IF  = 0;
    INTCON2bits.TMR0IP = 1;   /* high priority */
    INTCONbits.TMR0IE  = 1;

    T0CONbits.TMR0ON = 1;
}

/* ---- main ---------------------------------------------------------------- */
int main(void)
{
    system_init();
    buttons_init();
    lcd_init();
    game_init();
    timer0_init();

    /* Enable global and peripheral interrupts. */
    RCONbits.IPEN     = 1;   /* interrupt priority enable */
    INTCONbits.GIEH   = 1;   /* high-priority interrupts  */
    INTCONbits.GIEL   = 1;   /* low-priority interrupts   */

    for (;;) {
        /* Spin until the 30 Hz tick arrives. */
        if (!tick_flag)
            continue;
        tick_flag = 0;

        buttons_update();
        game_tick();
        game_render();
        lcd_flush();
    }

    return 0;
}
