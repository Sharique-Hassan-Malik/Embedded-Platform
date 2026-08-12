#ifndef FLASH_H
#define FLASH_H

#include <stdint.h>

/*
 * PIC18F4550 program flash read/write/erase driver.
 *
 * Erase granularity : 64 bytes per row (erase-then-rewrite unit)
 * Write granularity : 64 bytes per row
 * Read granularity  : 1 byte
 *
 * The PIC18 flash interface requires:
 *   - For erase: set EECON1 EEPGD=1, CFGS=0, FREE=1, WREN=1, then the
 *     mandatory unlock sequence, then WR=1.
 *   - For write: same but FREE=0 (and TABLAT preloaded via table write).
 *   Interrupts must be disabled during the unlock sequence.
 *
 * The bootloader occupies 0x0000–0x07FF and write-protects itself by
 * refusing to write to addresses below APP_START.
 */

#include "boot_shared.h"

/* Read one byte from program flash at `addr`. */
uint8_t flash_read_byte(uint32_t addr);

/* Erase one 64-byte row aligned to a 64-byte boundary.
 * addr must satisfy (addr & 0x3F) == 0 and addr >= APP_START.
 * Returns 0 on success, 1 on alignment/range violation. */
uint8_t flash_erase_row(uint32_t addr);

/* Write 64 bytes to a previously erased row.
 * addr must satisfy (addr & 0x3F) == 0 and addr >= APP_START.
 * Returns 0 on success, 1 on range/alignment violation. */
uint8_t flash_write_row(uint32_t addr, const uint8_t data[64]);

/* Read `len` bytes starting at `addr` into `buf`. */
void flash_read_block(uint32_t addr, uint8_t *buf, uint16_t len);

#endif /* FLASH_H */
