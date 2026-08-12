#include "dac.h"

#include <xc.h>

#define DAC_CS_PIN   0       /* RB0 */

#define DAC_CS_LOW()   (LATBCLR = (1u << DAC_CS_PIN))
#define DAC_CS_HIGH()  (LATBSET = (1u << DAC_CS_PIN))

void dac_init(void)
{
    /* /CS as digital output, deasserted (high). */
    TRISBCLR  = (1u << DAC_CS_PIN);
    ANSELBCLR = (1u << DAC_CS_PIN);
    DAC_CS_HIGH();

    /* Map SPI1 data output (SDO1) to RB5 via peripheral pin select. Without
     * this the MCP4921 receives no data. PPS is unlocked by default out of
     * reset (this firmware never sets CFGCON.IOLOCK), matching the U1RX map. */
    RPB5Rbits.RPB5R = 0b0011;   /* 0b0011 = SDO1 */

    /* Disable SPI1 before reconfiguring. */
    SPI1CONbits.ON = 0;

    /* Clear any stale data in the receive buffer. */
    (void)SPI1BUF;

    /* BRG = 1 → SPI clock = PBCLK / (2*(1+1)) = 40 MHz / 4 = 10 MHz. */
    SPI1BRG = 1;

    SPI1CON = 0;
    SPI1CONbits.MSTEN  = 1;   /* master mode */
    SPI1CONbits.CKE    = 1;   /* data changes on falling SCK edge (CPOL=0, CPHA=0) */
    SPI1CONbits.CKP    = 0;   /* SCK idle low */
    SPI1CONbits.MODE16 = 1;   /* 16-bit transfers */
    SPI1CONbits.ON     = 1;
}

void dac_write(uint16_t value)
{
    /* Build MCP4921 command word.
     * bit15=0: channel A
     * bit14=0: unbuffered Vref
     * bit13=1: 1x gain
     * bit12=1: output active
     * bits11:0: 12-bit sample */
    uint16_t word = 0x3000u | (value & 0x0FFFu);

    DAC_CS_LOW();
    SPI1BUF = word;
    while (SPI1STATbits.SPIBUSY)
        ;
    (void)SPI1BUF;   /* clear receive FIFO */
    DAC_CS_HIGH();
}
