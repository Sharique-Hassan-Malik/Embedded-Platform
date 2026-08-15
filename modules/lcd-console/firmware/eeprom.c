#include "eeprom.h"

#include <xc.h>

uint8_t ee_read_byte(uint8_t addr)
{
    EEADR          = addr;
    EECON1bits.EEPGD = 0;   /* access data EEPROM, not program memory */
    EECON1bits.CFGS  = 0;   /* access EEPROM, not config words        */
    EECON1bits.RD    = 1;   /* initiate read (clears on same cycle)   */
    NOP();                  /* required: one cycle after RD set       */
    NOP();
    return EEDATA;
}

void ee_write_byte(uint8_t addr, uint8_t data)
{
    uint8_t saved_gie;

    EEADR  = addr;
    EEDATA = data;

    EECON1bits.EEPGD = 0;
    EECON1bits.CFGS  = 0;
    EECON1bits.WREN  = 1;   /* enable write */

    /* The required unlock sequence must execute without interruption. */
    saved_gie          = INTCONbits.GIE;
    INTCONbits.GIE     = 0;

    EECON2 = 0x55;
    EECON2 = 0xAA;
    EECON1bits.WR = 1;      /* begin write cycle (~4 ms)              */

    INTCONbits.GIE = saved_gie;

    /* Busy-wait for write to complete. */
    while (EECON1bits.WR)
        ;

    EECON1bits.WREN = 0;    /* disable further writes until next call */
}

uint16_t eeprom_load_hiscore(void)
{
    if (ee_read_byte(EEPROM_MAGIC_ADDR) != EEPROM_MAGIC_VALUE)
        return 0u;

    return ((uint16_t)ee_read_byte(EEPROM_HISCORE_ADDR)     << 8) |
            (uint16_t)ee_read_byte(EEPROM_HISCORE_ADDR + 1u);
}

void eeprom_save_hiscore(uint16_t score)
{
    ee_write_byte(EEPROM_MAGIC_ADDR,      EEPROM_MAGIC_VALUE);
    ee_write_byte(EEPROM_HISCORE_ADDR,     (uint8_t)(score >> 8));
    ee_write_byte(EEPROM_HISCORE_ADDR + 1u,(uint8_t)(score & 0xFFu));
}
