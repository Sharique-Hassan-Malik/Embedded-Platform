#ifndef UART_H
#define UART_H

#include <stdint.h>

/*
 * Minimal polled UART driver for PIC16F1829.
 * Used by the demo application to stream touch events and raw counts
 * to a host terminal at 9 600 baud.
 *
 * Pin: RC4 = TX (EUSART TX, mapped via APFCON).
 * No RX needed for the demo.
 */

/* Initialise EUSART1 at 9 600 baud (Fosc = 16 MHz). */
void uart_init(void);

/* Transmit one byte (blocking). */
void uart_putc(char c);

/* Transmit a null-terminated string. */
void uart_puts(const char *s);

/* Transmit a decimal representation of a 16-bit unsigned integer. */
void uart_putu16(uint16_t v);

/* Transmit a decimal representation of a signed 16-bit integer. */
void uart_puti16(int16_t v);

#endif /* UART_H */
