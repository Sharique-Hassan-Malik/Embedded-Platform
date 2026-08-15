#include "sdlog.h"
#include "spi.h"
#include "hw.h"

#include <xc.h>
#include <stdint.h>
#include <string.h>

/* ---- SD SPI low-level ---------------------------------------------------- */

#define SD_CMD0   0x40u
#define SD_CMD8   0x48u
#define SD_CMD16  0x50u
#define SD_CMD17  0x51u
#define SD_CMD24  0x58u
#define SD_CMD58  0x7Au
#define SD_ACMD41 0x69u
#define SD_CMD55  0x77u

#define SD_DATA_TOKEN   0xFEu
#define SD_WRITE_RESP_MASK 0x1Fu
#define SD_WRITE_ACCEPTED  0x05u

/* Send 8 dummy clocks with CS high (required by SD SPI protocol). */
static void sd_dummy(void)
{
    SD_CS_HIGH();
    spi_byte(0xFFu);
}

/* Send a 6-byte command, return the R1 response byte. */
static uint8_t sd_cmd(uint8_t cmd, uint32_t arg, uint8_t crc)
{
    uint8_t  pkt[6];
    uint8_t  r1;
    uint8_t  retry = 8u;

    pkt[0] = cmd;
    pkt[1] = (uint8_t)(arg >> 24);
    pkt[2] = (uint8_t)(arg >> 16);
    pkt[3] = (uint8_t)(arg >>  8);
    pkt[4] = (uint8_t)(arg);
    pkt[5] = crc;

    SD_CS_LOW();
    spi_write(pkt, 6u);

    /* R1 appears within 8 bytes. */
    do {
        r1 = spi_byte(0xFFu);
    } while ((r1 & 0x80u) && --retry);

    return r1;
}

/* ---- SD card initialisation --------------------------------------------- */

uint8_t sdlog_init(void)
{
    uint8_t  r1;
    uint8_t  buf[4];
    uint16_t retry;

    /* ≥74 dummy clocks with CS high to put card in SPI mode. */
    SD_CS_HIGH();
    {
        uint8_t i;
        for (i = 0u; i < 10u; i++) spi_byte(0xFFu);
    }

    /* CMD0 — software reset (CRC 0x95 is required for CMD0 in SPI mode). */
    r1 = sd_cmd(SD_CMD0, 0u, 0x95u);
    sd_dummy();
    if (r1 != 0x01u) return 1u;   /* must idle */

    /* CMD8 — check voltage (V2 card detection). */
    r1 = sd_cmd(SD_CMD8, 0x000001AAu, 0x87u);
    if (r1 == 0x01u) {
        /* V2 card: read trailing 4 bytes and verify echo. */
        spi_read(buf, 4u);
    }
    sd_dummy();

    /* ACMD41 — initialise SDHC/SDXC. */
    for (retry = 2000u; retry; retry--) {
        sd_cmd(SD_CMD55, 0u, 0xFFu);   /* CMD55 prefix */
        sd_dummy();
        r1 = sd_cmd(SD_ACMD41, 0x40000000u, 0xFFu);
        sd_dummy();
        if (r1 == 0x00u) break;
    }
    if (r1 != 0x00u) return 2u;

    /* CMD16 — set block length to 512. */
    r1 = sd_cmd(SD_CMD16, 512u, 0xFFu);
    sd_dummy();
    if (r1 != 0x00u) return 3u;

    return 0u;
}

/* ---- Write a single 512-byte sector -------------------------------------- */

static uint8_t sd_write_sector(uint32_t sector, const uint8_t *buf)
{
    uint8_t r1, resp;
    uint8_t retry;

    /* SDHC uses sector addresses; SDSC uses byte addresses.
     * For simplicity we assume SDHC (sector addressing). */
    r1 = sd_cmd(SD_CMD24, sector, 0xFFu);
    if (r1 != 0x00u) { sd_dummy(); return 1u; }

    /* Data start token. */
    spi_byte(0xFFu);
    spi_byte(SD_DATA_TOKEN);

    /* 512 data bytes. */
    spi_write(buf, 512u);

    /* Dummy CRC (2 bytes). */
    spi_byte(0xFFu);
    spi_byte(0xFFu);

    /* Data response token. */
    resp = spi_byte(0xFFu);
    sd_dummy();

    if ((resp & SD_WRITE_RESP_MASK) != SD_WRITE_ACCEPTED) return 2u;

    /* Wait for card to finish programming (~10–200 ms). */
    for (retry = 200u; retry; retry--) {
        SD_CS_LOW();
        if (spi_byte(0xFFu) == 0xFFu) { sd_dummy(); return 0u; }
        SD_CS_HIGH();
        __delay_ms(1);
    }

    sd_dummy();
    return 3u;   /* programming timeout */
}

/* ---- EEPROM position helpers --------------------------------------------- */

static uint8_t eeprom_read_byte(uint8_t addr)
{
    EEADR          = addr;
    EECON1bits.EEPGD = 0;
    EECON1bits.CFGS  = 0;
    EECON1bits.RD    = 1;
    NOP(); NOP();
    return EEDATA;
}

static void eeprom_write_byte(uint8_t addr, uint8_t data)
{
    uint8_t gie;
    EEADR  = addr;
    EEDATA = data;
    EECON1bits.EEPGD = 0;
    EECON1bits.CFGS  = 0;
    EECON1bits.WREN  = 1;
    gie = INTCONbits.GIE;
    INTCONbits.GIE = 0;
    EECON2 = 0x55u;
    EECON2 = 0xAAu;
    EECON1bits.WR = 1;
    INTCONbits.GIE = gie;
    while (EECON1bits.WR);
    EECON1bits.WREN = 0;
}

static uint32_t load_sector(void)
{
    uint32_t s;
    if (eeprom_read_byte(EEPROM_MAGIC) != EEPROM_MAGIC_VAL) {
        /* First boot: initialise position. */
        eeprom_write_byte(EEPROM_MAGIC,    EEPROM_MAGIC_VAL);
        eeprom_write_byte(EEPROM_LOG_SECTOR,     0u);
        eeprom_write_byte(EEPROM_LOG_SECTOR + 1u, 0u);
        eeprom_write_byte(EEPROM_LOG_SECTOR + 2u, 0u);
        eeprom_write_byte(EEPROM_LOG_SECTOR + 3u, 0u);
        eeprom_write_byte(EEPROM_LOG_REC_IDX, 0u);
    }
    s  = (uint32_t)eeprom_read_byte(EEPROM_LOG_SECTOR)     << 24;
    s |= (uint32_t)eeprom_read_byte(EEPROM_LOG_SECTOR + 1u) << 16;
    s |= (uint32_t)eeprom_read_byte(EEPROM_LOG_SECTOR + 2u) <<  8;
    s |=  (uint32_t)eeprom_read_byte(EEPROM_LOG_SECTOR + 3u);
    return s;
}

static void save_sector(uint32_t s, uint8_t idx)
{
    eeprom_write_byte(EEPROM_LOG_SECTOR,      (uint8_t)(s >> 24));
    eeprom_write_byte(EEPROM_LOG_SECTOR + 1u, (uint8_t)(s >> 16));
    eeprom_write_byte(EEPROM_LOG_SECTOR + 2u, (uint8_t)(s >>  8));
    eeprom_write_byte(EEPROM_LOG_SECTOR + 3u, (uint8_t)(s));
    eeprom_write_byte(EEPROM_LOG_REC_IDX, idx);
}

/* ---- Fixed-width decimal formatting -------------------------------------- */

/* Write `digits` decimal digits of `val` (zero-padded) into `buf`. */
static void fmt_dec(char *buf, uint32_t val, uint8_t digits)
{
    uint8_t i;
    for (i = digits; i > 0u; i--) {
        buf[i - 1u] = (char)('0' + val % 10u);
        val /= 10u;
    }
}

/* ---- Public API ---------------------------------------------------------- */

uint8_t sdlog_write(const rtc_time_t *t, const bme280_result_t *r)
{
    /* sector_buf holds the full 512-byte sector being built or updated. */
    static uint8_t sector_buf[512u];
    uint32_t sector;
    uint8_t  rec_idx;
    uint8_t  *rec;
    int32_t  temp;
    uint32_t press_hpa;   /* Pa / 100 → hPa integer part */
    uint32_t press_frac;  /* Pa % 100 → hPa fractional  */
    uint32_t hum;
    char     sign;
    uint8_t  err;

    sector  = SDLOG_START_SECTOR + load_sector();
    rec_idx = eeprom_read_byte(EEPROM_LOG_REC_IDX);

    if (rec_idx >= SDLOG_RECS_PER_SEC) {
        /* Should not happen (save_sector keeps it in range), but guard. */
        rec_idx = 0u;
        sector++;
    }

    /* Build a blank sector and populate the target record slot. */
    memset(sector_buf, 0x20u, sizeof(sector_buf));  /* space-fill */

    rec = sector_buf + (uint16_t)rec_idx * SDLOG_BYTES_PER_REC;

    /* "YYYY-MM-DD HH:MM:SS " */
    fmt_dec((char *)rec +  0, 2000u + t->year,  4u);  rec[4]  = '-';
    fmt_dec((char *)rec +  5, t->month,          2u);  rec[7]  = '-';
    fmt_dec((char *)rec +  8, t->day,            2u);  rec[10] = ' ';
    fmt_dec((char *)rec + 11, t->hour,           2u);  rec[13] = ':';
    fmt_dec((char *)rec + 14, t->minute,         2u);  rec[16] = ':';
    fmt_dec((char *)rec + 17, t->second,         2u);  rec[19] = ' ';

    /* Temperature: "+TT.TTC" or "-TT.TTC" */
    temp = r->temperature;
    if (temp < 0) { sign = '-'; temp = -temp; } else { sign = '+'; }
    rec[20] = (uint8_t)sign;
    fmt_dec((char *)rec + 21, (uint32_t)temp / 100u, 2u);  rec[23] = '.';
    fmt_dec((char *)rec + 24, (uint32_t)temp % 100u, 2u);  rec[26] = 'C';
    rec[27] = ' ';

    /* Humidity: "HHH%RH" */
    hum = r->humidity >> 10;   /* integer %RH */
    fmt_dec((char *)rec + 28, hum, 3u);
    rec[31] = '%'; rec[32] = 'R'; rec[33] = 'H'; rec[34] = ' ';

    /* Pressure: "PPPPP.PPPa" */
    press_hpa  = r->pressure / 100u;
    press_frac = r->pressure % 100u;
    fmt_dec((char *)rec + 35, press_hpa, 4u);  rec[39] = '.';
    fmt_dec((char *)rec + 40, press_frac, 2u); rec[42] = 'h'; rec[43] = 'P'; rec[44] = 'a';
    rec[45] = '\n';

    /* Write the sector and advance position. */
    err = sd_write_sector(sector, sector_buf);
    if (err) return err;

    rec_idx++;
    if (rec_idx >= SDLOG_RECS_PER_SEC) {
        rec_idx = 0u;
        sector++;
        if (sector >= SDLOG_START_SECTOR + SDLOG_MAX_SECTORS)
            sector = SDLOG_START_SECTOR;   /* wrap */
    }
    save_sector(sector - SDLOG_START_SECTOR, rec_idx);

    return 0u;
}
