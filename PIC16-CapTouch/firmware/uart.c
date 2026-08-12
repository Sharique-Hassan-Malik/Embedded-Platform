#include "uart.h"

#include <xc.h>

/*
 * EUSART1 at 9 600 baud, 8N1.
 * BRGH = 1, BRG16 = 1 → SPBRG = (Fosc / (4 × baud)) − 1
 *                             = (16 000 000 / (4 × 9 600)) − 1 = 415
 */
#define BAUD_SPBRG  415u

void uart_init(void)
{
    /* Map TX/CK to RC4 via Alternate Pin Function Control. */
    APFCON0bits.TXCKSEL = 1u;   /* 1 = TX/CK on RC4, 0 = RB7 */

    /* RC4 as output. RC4 is digital-only on the 16F1829 (no ANSC4). */
    TRISCbits.TRISC4 = 0u;

    SPBRGH = (uint8_t)(BAUD_SPBRG >> 8);
    SPBRGL = (uint8_t)(BAUD_SPBRG & 0xFFu);

    BAUDCON = 0x08u;   /* BRG16 = 1 */
    TXSTA   = 0x24u;   /* BRGH = 1, SYNC = 0, TXEN = 1 */
    RCSTA   = 0x80u;   /* SPEN = 1 */
}

void uart_putc(char c)
{
    while (!TXSTAbits.TRMT)
        ;
    TXREG = (uint8_t)c;
}

void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

void uart_putu16(uint16_t v)
{
    char    buf[6];
    uint8_t i = 5u;

    buf[i] = '\0';
    if (v == 0u) {
        buf[--i] = '0';
    } else {
        while (v > 0u && i > 0u) {
            buf[--i] = (char)('0' + v % 10u);
            v       /= 10u;
        }
    }
    uart_puts(&buf[i]);
}

void uart_puti16(int16_t v)
{
    if (v < 0) {
        uart_putc('-');
        uart_putu16((uint16_t)(-v));
    } else {
        uart_putu16((uint16_t)v);
    }
}
