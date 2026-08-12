#ifndef SDLOG_H
#define SDLOG_H

#include <stdint.h>

/*
 * Raw-sector SD card logger for captured protocol frames.
 *
 * Each log record is 16 bytes:
 *   uint32_t  timestamp_us   — Timer1-based microsecond counter (wraps at ~71 min)
 *   uint32_t  capture_word   — the packed capture word from hw.h
 *   uint8_t[8] reserved      — zero-padded
 *
 * 32 records pack into one 512-byte sector (16 × 32 = 512).
 * Logging starts at sector 2048; position persisted in EEPROM.
 *
 * On the host, extract with:
 *   python3 host/decode.py --sd /dev/sdX --out capture.txt
 */

#define SDLOG_REC_SIZE    16u
#define SDLOG_RECS_PER_S  32u
#define SDLOG_START_SEC   2048u
#define SDLOG_MAX_SECS    65000u

/* Initialise SD card (CMD0/CMD8/ACMD41/CMD16). */
uint8_t sdlog_init(void);

/* Write one capture word with its timestamp to the SD log. */
uint8_t sdlog_write(uint32_t timestamp_us, uint32_t capture_word);

/* Flush any buffered sector to SD. */
uint8_t sdlog_flush(void);

#endif /* SDLOG_H */
