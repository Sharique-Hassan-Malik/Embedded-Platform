/*
 * app_template/main.c — Minimal application skeleton for the PIC18 secure
 * bootloader.
 *
 * The application must be linked so that:
 *   - Code origin   = 0x0800 (APP_START)
 *   - High ISR      = 0x0808
 *   - Low  ISR      = 0x0818
 *
 * MPLAB X linker script change (app.lkr):
 *   CODEPAGE NAME=vectors  START=0x800  END=0x81F  PROTECTED
 *   CODEPAGE NAME=page     START=0x820  END=0x7BFF
 *
 * The bootloader's own vectors at 0x0008/0x0018 are not overwritten; the
 * bootloader itself does not use interrupts during the update window.
 *
 * After signing (sign_and_flash.py sign) the metadata block at 0x7C00 is
 * populated automatically.  The application never writes to 0x7C00–0x7FFF.
 */

#define _XTAL_FREQ  8000000UL   /* 8 MHz internal oscillator — for __delay_*() */

#include <xc.h>
#include <stdint.h>

/* Remapped high-priority interrupt vector. */
void __interrupt(high_priority) app_isr_high(void)
{
    /* Add interrupt handlers here. */
}

/* Remapped low-priority interrupt vector. */
void __interrupt(low_priority) app_isr_low(void)
{
    /* Add interrupt handlers here. */
}

void main(void)
{
    /* Application starts here.  The bootloader has already verified the
     * ECDSA-P256 signature before reaching this point, so this code is
     * guaranteed to be unmodified relative to the signed image. */

    /* Example: blink an LED on RD0. */
    TRISDbits.TRISD0 = 0;

    for (;;) {
        LATDbits.LATD0 = 1;
        __delay_ms(500);
        LATDbits.LATD0 = 0;
        __delay_ms(500);
    }
}
