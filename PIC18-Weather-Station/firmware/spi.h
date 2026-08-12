#ifndef SPI_H
#define SPI_H

#include <stdint.h>

/*
 * Hardware SPI master driver using MSSP2 on PIC18F26K22.
 *
 * Clock rate at Fosc = 1 MHz:
 *   EPD mode (low speed)  : Fosc/16 = 62.5 kHz  — SSD1680 min 1 kHz, no max
 *   SD  mode (high speed) : Fosc/4  = 250 kHz    — used for SD card init only
 *
 * Both the e-ink CS and SD CS are managed by the callers, not this driver.
 * The bus is shared; the caller must deassert the inactive device's CS before
 * asserting the active one.
 *
 * SPI mode 0 (CPOL = 0, CPHA = 0) is used throughout.
 */

void spi_init(void);

/* Transfer one byte (sends and receives simultaneously). */
uint8_t spi_byte(uint8_t out);

/* Send `len` bytes from `buf`, discard received bytes. */
void spi_write(const uint8_t *buf, uint16_t len);

/* Receive `len` bytes into `buf`, send 0xFF throughout. */
void spi_read(uint8_t *buf, uint16_t len);

#endif /* SPI_H */
