#ifndef SDLOG_H
#define SDLOG_H

#include <stdint.h>
#include "bme280.h"
#include "ds3231.h"

/*
 * Raw-sector SD card logger.
 *
 * No filesystem is used.  Records are written to sequential 512-byte sectors
 * starting at SDLOG_START_SECTOR (defined in hw.h).  Each record is exactly
 * SDLOG_BYTES_PER_REC (64) bytes, yielding 8 records per sector.
 *
 * The current write position (sector number + record index within sector) is
 * stored in PIC18 internal EEPROM so it survives power cycles.
 *
 * Record format (64 bytes, space-padded, newline-terminated):
 *   "YYYY-MM-DD HH:MM:SS +TT.TTC HHH%RH PPPPP.PPPa\n              "
 *
 * Example:
 *   "2024-06-15 08:30:00 +24.15C 065%RH 96386Pa\n                  "
 *
 * The host can recover all records by reading the raw SD card image:
 *   dd if=/dev/sdX bs=512 skip=2048 count=64000 | grep -a "^20"
 */

/* Initialise the SD card (CMD0, CMD8, ACMD41, CMD58, CMD16).
 * Returns 0 on success. */
uint8_t sdlog_init(void);

/*
 * Write one weather record.
 * Reads the write position from EEPROM, writes a 64-byte record to the SD,
 * then advances and saves the position.
 * Returns 0 on success.
 */
uint8_t sdlog_write(const rtc_time_t *t, const bme280_result_t *r);

#endif /* SDLOG_H */
