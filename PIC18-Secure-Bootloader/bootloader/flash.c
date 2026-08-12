#include "flash.h"

#include <xc.h>
#include <stdint.h>

/* ---- Table-read helper --------------------------------------------------- */

uint8_t flash_read_byte(uint32_t addr)
{
    TBLPTRU = (uint8_t)(addr >> 16);
    TBLPTRH = (uint8_t)(addr >>  8);
    TBLPTRL = (uint8_t)(addr);
    asm("TBLRD*");
    return TABLAT;
}

void flash_read_block(uint32_t addr, uint8_t *buf, uint16_t len)
{
    uint16_t i;
    TBLPTRU = (uint8_t)(addr >> 16);
    TBLPTRH = (uint8_t)(addr >>  8);
    TBLPTRL = (uint8_t)(addr);
    for (i = 0u; i < len; i++) {
        asm("TBLRD*+");
        buf[i] = TABLAT;
    }
}

/* ---- Unlock sequence ----------------------------------------------------- */

static void nvm_unlock_and_write(void)
{
    uint8_t saved_gie = INTCONbits.GIE;
    INTCONbits.GIE = 0u;
    EECON2 = 0x55u;
    EECON2 = 0xAAu;
    EECON1bits.WR = 1u;
    /* WR clears automatically after the write cycle. */
    INTCONbits.GIE = saved_gie;
}

/* ---- Row erase ----------------------------------------------------------- */

uint8_t flash_erase_row(uint32_t addr)
{
    if (addr & 0x3Fu)        return 1u;   /* must be row-aligned */
    if (addr < APP_START)    return 1u;   /* protect bootloader  */

    TBLPTRU = (uint8_t)(addr >> 16);
    TBLPTRH = (uint8_t)(addr >>  8);
    TBLPTRL = (uint8_t)(addr);

    EECON1 = 0u;
    EECON1bits.EEPGD = 1u;   /* target: program flash */
    EECON1bits.CFGS  = 0u;
    EECON1bits.FREE  = 1u;   /* erase operation */
    EECON1bits.WREN  = 1u;

    nvm_unlock_and_write();

    while (EECON1bits.WR)
        ;

    EECON1bits.WREN = 0u;
    EECON1bits.FREE = 0u;
    return 0u;
}

/* ---- Row write ----------------------------------------------------------- */

uint8_t flash_write_row(uint32_t addr, const uint8_t data[64])
{
    uint8_t i;

    if (addr & 0x3Fu)        return 1u;
    if (addr < APP_START)    return 1u;

    /* Load the 64-byte holding register via table writes. */
    TBLPTRU = (uint8_t)(addr >> 16);
    TBLPTRH = (uint8_t)(addr >>  8);
    TBLPTRL = (uint8_t)(addr);

    for (i = 0u; i < 64u; i++) {
        TABLAT = data[i];
        if (i < 63u) {
            asm("TBLWT*+");   /* write with post-increment */
        } else {
            asm("TBLWT*");    /* last byte: no increment */
        }
    }

    EECON1 = 0u;
    EECON1bits.EEPGD = 1u;
    EECON1bits.CFGS  = 0u;
    EECON1bits.FREE  = 0u;   /* write, not erase */
    EECON1bits.WREN  = 1u;

    nvm_unlock_and_write();

    while (EECON1bits.WR)
        ;

    EECON1bits.WREN = 0u;
    return 0u;
}
