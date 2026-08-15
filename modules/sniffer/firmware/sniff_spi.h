#ifndef SNIFF_SPI_H
#define SNIFF_SPI_H

#include <stdint.h>

/*
 * Passive SPI sniffer using SPI1 hardware in slave mode (DISSDO = 1).
 *
 * Captures:
 *   /CS assertion   → FLAG_SPI_CS_ASSERT  word
 *   MOSI byte       → FLAG_SPI_MOSI       word
 *   MISO byte       → FLAG_SPI_MISO       word (captured in parallel)
 *   /CS deassertion → FLAG_SPI_CS_DEASSERT word
 *
 * Supports CPOL=0 CPHA=0 (mode 0) and CPOL=1 CPHA=1 (mode 3).
 * CPOL and CPHA are runtime-configurable.
 */

typedef enum { SPI_MODE_0 = 0, SPI_MODE_3 = 3 } spi_mode_t;

void sniff_spi_init(spi_mode_t mode);
void sniff_spi_set_mode(spi_mode_t mode);

#endif /* SNIFF_SPI_H */
