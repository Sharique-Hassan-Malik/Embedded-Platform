#ifndef SNIFF_UART_H
#define SNIFF_UART_H

#include <stdint.h>

/*
 * Passive UART sniffer using UART2 receiver on PIC24.
 * Captures target TX bytes into the shared circular buffer (hw.h).
 * Framing errors are reported via FLAG_UART_FRAMING_ERR.
 *
 * Default baud rate: 115 200.  Supported rates:
 *   9 600, 19 200, 38 400, 57 600, 115 200, 250 000, 500 000.
 */

/* Initialise UART2 receiver at the given baud rate. */
void sniff_uart_init(uint32_t baud);

/* Change baud rate without power cycle. */
void sniff_uart_set_baud(uint32_t baud);

#endif /* SNIFF_UART_H */
