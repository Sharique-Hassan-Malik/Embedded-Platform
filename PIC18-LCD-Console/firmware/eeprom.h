#ifndef EEPROM_H
#define EEPROM_H

#include <stdint.h>

/*
 * PIC18F4550 internal data EEPROM access (256 bytes, addresses 0x00–0xFF).
 *
 * High score layout:
 *   Address 0x00 — magic byte (0xA5 = initialised)
 *   Address 0x01 — high score high byte
 *   Address 0x02 — high score low byte
 */

#define EEPROM_MAGIC_ADDR    0x00u
#define EEPROM_HISCORE_ADDR  0x01u
#define EEPROM_MAGIC_VALUE   0xA5u

/* Read a single byte from EEPROM. */
uint8_t ee_read_byte(uint8_t addr);

/* Write a single byte to EEPROM (blocks until write cycle completes). */
void ee_write_byte(uint8_t addr, uint8_t data);

/* Read the 16-bit high score, or return 0 if EEPROM has not been
 * initialised (magic byte absent). */
uint16_t eeprom_load_hiscore(void);

/* Write a new high score and stamp the magic byte. */
void eeprom_save_hiscore(uint16_t score);

#endif /* EEPROM_H */
