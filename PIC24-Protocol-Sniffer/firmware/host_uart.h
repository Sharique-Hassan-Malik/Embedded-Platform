#ifndef HOST_UART_H
#define HOST_UART_H

#include <stdint.h>

/*
 * Host-side output UART (UART1) at 115 200 baud on RP2/RB2.
 * Used to stream decoded capture frames to a USB-serial adapter
 * for live display in the host Python tool.
 *
 * Output format (one line per captured event):
 *   "U <hex_byte>[ FE]\n"                              — UART byte (FE = framing error)
 *   "I S\n"                                            — I2C START
 *   "I P\n"                                            — I2C STOP
 *   "I A <hex_addr> <R|W> <ACK|NAK>\n"                — I2C address phase
 *   "I D <hex_byte> <ACK|NAK>\n"                       — I2C data byte
 *   "S CS\n"  "S CD\n"                                — SPI /CS assert / deassert
 *   "S O <hex_byte>\n"  "S I <hex_byte>\n"             — SPI MOSI / MISO
 */

void host_uart_init(void);
void host_uart_putc(char c);
void host_uart_puts(const char *s);
void host_uart_put_hex(uint8_t v);
void host_uart_put_u32_hex(uint32_t v);

/* Decode and print one capture word. */
void host_uart_print_word(uint32_t word);

#endif /* HOST_UART_H */
