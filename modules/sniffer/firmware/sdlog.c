#include "sdlog.h"
#include "hw.h"

#include <xc.h>
#include <stdint.h>
#include <string.h>

/* ---- SPI2 helpers for SD card -------------------------------------------- */

static uint8_t spi2_byte(uint8_t out)
{
    SPI2BUF = out;
    while (!SPI2STATbits.SPIRBF);
    return (uint8_t)SPI2BUF;
}

static uint8_t sd_cmd(uint8_t cmd, uint32_t arg, uint8_t crc)
{
    uint8_t pkt[6] = {
        cmd,
        (uint8_t)(arg >> 24), (uint8_t)(arg >> 16),
        (uint8_t)(arg >>  8), (uint8_t)(arg),
        crc
    };
    uint8_t r1; uint8_t retry = 8u;
    SD_CS_LOW();
    { uint8_t i; for (i = 0u; i < 6u; i++) spi2_byte(pkt[i]); }
    do { r1 = spi2_byte(0xFFu); } while ((r1 & 0x80u) && --retry);
    return r1;
}

static void sd_deselect(void) { SD_CS_HIGH(); spi2_byte(0xFFu); }

/* ---- Persistent position in EEPROM (simulated via RAM for PIC24) --------- */
/* PIC24FJ64GA002 has no internal EEPROM; use last flash page for state.
 * For this implementation we keep the position in RAM (non-volatile across
 * warm resets via NVM is not implemented here to keep the code portable).
 * In a production build, write to the last erase page of flash. */

static uint32_t log_sector  = SDLOG_START_SEC;
static uint8_t  log_rec_idx = 0u;
static uint8_t  sector_buf[512u];
static uint8_t  sector_dirty = 0u;

/* ---- SD init ------------------------------------------------------------- */

uint8_t sdlog_init(void)
{
    uint8_t r1; uint16_t retry;

    /* SPI2 at ~250 kHz for init. */
    TRISBbits.TRISB14 = 0u;   /* SCK  output */
    TRISBbits.TRISB15 = 0u;   /* MOSI output */
    TRISBbits.TRISB4  = 1u;   /* MISO input  */

    __builtin_write_OSCCONL(OSCCON & 0xBFu);
    RPOR7bits.RP14R  = 0x0Bu;   /* RP14 = SCK2 */
    RPOR7bits.RP15R  = 0x0Au;   /* RP15 = SDO2 */
    RPINR22bits.SDI2R = 4u;     /* RP4 = SDI2  */
    __builtin_write_OSCCONL(OSCCON | 0x40u);

    SD_CS_TRIS = 0u; SD_CS_HIGH();

    SPI2CON1 = 0x0100u;   /* MSTEN=1, 8-bit, Fcy/64 = 250 kHz */
    SPI2CON1bits.MODE16 = 0u;
    SPI2CON1bits.MSTEN  = 1u;
    SPI2CON1bits.PPRE   = 0u;   /* primary prescaler 64:1 */
    SPI2CON1bits.SPRE   = 7u;   /* secondary prescaler 1:1 */
    SPI2STATbits.SPIEN  = 1u;

    { uint8_t i; SD_CS_HIGH(); for (i = 0u; i < 10u; i++) spi2_byte(0xFFu); }

    r1 = sd_cmd(0x40u, 0u, 0x95u);
    sd_deselect();
    if (r1 != 0x01u) return 1u;

    r1 = sd_cmd(0x48u, 0x000001AAu, 0x87u);
    if (r1 == 0x01u) { uint8_t b[4]; uint8_t i; for (i=0;i<4;i++) b[i]=spi2_byte(0xFFu); }
    sd_deselect();

    for (retry = 2000u; retry; retry--) {
        sd_cmd(0x77u, 0u, 0xFFu); sd_deselect();
        r1 = sd_cmd(0x69u, 0x40000000u, 0xFFu);
        sd_deselect();
        if (r1 == 0x00u) break;
    }
    if (r1 != 0x00u) return 2u;

    r1 = sd_cmd(0x50u, 512u, 0xFFu);
    sd_deselect();

    memset(sector_buf, 0, sizeof(sector_buf));
    return 0u;
}

/* ---- Write one sector ---------------------------------------------------- */

static uint8_t sd_write_sector(uint32_t sector)
{
    uint8_t r1, resp; uint8_t retry = 200u;
    r1 = sd_cmd(0x58u, sector, 0xFFu);
    if (r1) { sd_deselect(); return 1u; }
    spi2_byte(0xFFu);
    spi2_byte(0xFEu);
    { uint16_t i; for (i = 0u; i < 512u; i++) spi2_byte(sector_buf[i]); }
    spi2_byte(0xFFu); spi2_byte(0xFFu);
    resp = spi2_byte(0xFFu);
    sd_deselect();
    if ((resp & 0x1Fu) != 0x05u) return 2u;
    while (retry--) {
        SD_CS_LOW();
        if (spi2_byte(0xFFu) == 0xFFu) { sd_deselect(); return 0u; }
        SD_CS_HIGH();
    }
    return 3u;
}

/* ---- Public API ---------------------------------------------------------- */

uint8_t sdlog_write(uint32_t ts, uint32_t cap_word)
{
    uint8_t *rec = sector_buf + (uint16_t)log_rec_idx * SDLOG_REC_SIZE;

    rec[0]  = (uint8_t)(ts >> 24);
    rec[1]  = (uint8_t)(ts >> 16);
    rec[2]  = (uint8_t)(ts >>  8);
    rec[3]  = (uint8_t)(ts);
    rec[4]  = (uint8_t)(cap_word >> 24);
    rec[5]  = (uint8_t)(cap_word >> 16);
    rec[6]  = (uint8_t)(cap_word >>  8);
    rec[7]  = (uint8_t)(cap_word);
    memset(rec + 8u, 0, 8u);

    sector_dirty = 1u;
    log_rec_idx++;

    if (log_rec_idx >= SDLOG_RECS_PER_S) {
        uint8_t err = sd_write_sector(log_sector);
        memset(sector_buf, 0, sizeof(sector_buf));
        log_rec_idx   = 0u;
        log_sector++;
        sector_dirty  = 0u;
        if (log_sector >= SDLOG_START_SEC + SDLOG_MAX_SECS)
            log_sector = SDLOG_START_SEC;
        return err;
    }
    return 0u;
}

uint8_t sdlog_flush(void)
{
    if (!sector_dirty) return 0u;
    uint8_t err = sd_write_sector(log_sector);
    sector_dirty = 0u;
    return err;
}
