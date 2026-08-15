#ifndef DAC_H
#define DAC_H

#include <stdint.h>

/*
 * Initialize SPI1 for the MCP4921 12-bit DAC.
 *
 * Pin assignments (PIC32MX270F256B):
 *   RB0  — /CS  (active-low chip select, GPIO output)
 *   RB5  — SDO1 (MOSI)
 *   RB3  — SCK1 (clock)
 *   RB2  — SDI1 (MISO, not used by MCP4921 but pin reserved)
 *
 * SPI clock: PBCLK(40 MHz) / (2 * (BRG + 1)) = 10 MHz.
 * MCP4921 maximum SPI clock is 20 MHz, so 10 MHz is safe across temperature.
 */
void dac_init(void);

/*
 * Write a 12-bit sample to MCP4921 DAC channel A.
 *   value : unsigned 12-bit (0 = 0 V, 4095 = Vref)
 *
 * Command word sent over SPI:
 *   bit 15   : 0  — select channel A
 *   bit 14   : 0  — unbuffered Vref input
 *   bit 13   : 1  — 1× gain  (~Vref output range)
 *   bit 12   : 1  — /SHDN = 1 (output active)
 *   bits 11:0: 12-bit data
 */
void dac_write(uint16_t value);

/*
 * Convert a signed Q15 audio sample to a 12-bit unsigned DAC value.
 * Maps [-32 768, +32 767] → [0, 4095] with midpoint at 2048.
 */
static inline uint16_t q15_to_dac(int32_t sample)
{
    int32_t shifted = sample + 32768;
    if (shifted < 0)      shifted = 0;
    if (shifted > 65535)  shifted = 65535;
    return (uint16_t)((uint32_t)shifted >> 4);
}

#endif /* DAC_H */
