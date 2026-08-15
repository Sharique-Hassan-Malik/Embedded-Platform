#ifndef IHEX_H
#define IHEX_H

#include <stdint.h>

/*
 * Intel HEX receive and parse engine.
 *
 * Reads lines from UART, validates each record checksum and dispatches:
 *   DATA records    → buffered in a 64-byte row accumulator, flushed to
 *                     flash when the row boundary is crossed.
 *   EOF record      → returns IHEX_DONE.
 *   EXT_ADDR record → updates the upper 16-bit address segment.
 *
 * Rejects:
 *   - Records with bad checksums  (sends UART_NAK, continues)
 *   - Writes into bootloader space (0x0000–0x07FF)
 *   - Writes to the config page    (0x7E00–0x7FFF) — public key is immutable
 *
 * Returns:
 *   IHEX_DONE   (0) — EOF record received; all rows written to flash
 *   IHEX_ERROR  (1) — fatal error (flash write failure or UART timeout)
 */

#define IHEX_DONE   0u
#define IHEX_ERROR  1u

uint8_t ihex_receive(void);

#endif /* IHEX_H */
