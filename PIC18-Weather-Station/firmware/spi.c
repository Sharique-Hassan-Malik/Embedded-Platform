#include "spi.h"
#include "hw.h"

#include <xc.h>
#include <stdint.h>

void spi_init(void)
{
    /* RB1 = SCK2 output, RB2 = SDO2 output, RB3 = SDI2 input. */
    TRISBbits.TRISB1 = 0;
    TRISBbits.TRISB2 = 0;
    TRISBbits.TRISB3 = 1;
    ANSELBbits.ANSB1 = 0;
    ANSELBbits.ANSB2 = 0;
    ANSELBbits.ANSB3 = 0;

    /* CS and control pins for e-ink. */
    EPD_CS_TRIS   = 0;  EPD_CS_HIGH();
    EPD_DC_TRIS   = 0;  EPD_DC_CMD();
    EPD_RST_TRIS  = 0;  EPD_RST_HIGH();
    EPD_BUSY_TRIS = 1;
    ANSELAbits.ANSA3 = 0;   /* EPD_BUSY = RA3 — digital input */

    /* SD CS. */
    SD_CS_TRIS = 0;  SD_CS_HIGH();

    /* MSSP2: master SPI mode 0, Fosc/16 = 62.5 kHz. */
    SSP2CON1 = 0x21u;   /* SSPEN=1, CKP=0, SSPM=0001 (Fosc/16) */
    SSP2STAT = 0x40u;   /* SMP=0 (sample mid), CKE=1 (CPHA=0)  */
}

uint8_t spi_byte(uint8_t out)
{
    SSP2BUF = out;
    while (!SSP2STATbits.BF)
        ;
    return SSP2BUF;
}

void spi_write(const uint8_t *buf, uint16_t len)
{
    uint16_t i;
    for (i = 0u; i < len; i++) {
        SSP2BUF = buf[i];
        while (!SSP2STATbits.BF)
            ;
        (void)SSP2BUF;
    }
}

void spi_read(uint8_t *buf, uint16_t len)
{
    uint16_t i;
    for (i = 0u; i < len; i++) {
        SSP2BUF = 0xFFu;
        while (!SSP2STATbits.BF)
            ;
        buf[i] = SSP2BUF;
    }
}
