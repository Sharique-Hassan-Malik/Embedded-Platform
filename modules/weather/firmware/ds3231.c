#include "ds3231.h"
#include "i2c.h"
#include "hw.h"

/* ---- DS3231 register map ------------------------------------------------- */
#define DS_REG_SECONDS   0x00u
#define DS_REG_MINUTES   0x01u
#define DS_REG_HOURS     0x02u
#define DS_REG_DOW       0x03u   /* day of week (unused) */
#define DS_REG_DATE      0x04u
#define DS_REG_MONTH     0x05u
#define DS_REG_YEAR      0x06u
#define DS_REG_ALM2_MIN  0x0Bu
#define DS_REG_ALM2_HR   0x0Cu
#define DS_REG_ALM2_DAY  0x0Du
#define DS_REG_CONTROL   0x0Eu
#define DS_REG_STATUS    0x0Fu

/* CONTROL register bits */
#define DS_CTRL_INTCN    0x04u   /* 1 = INT/SQW pin driven by alarm     */
#define DS_CTRL_A2IE     0x02u   /* Alarm 2 interrupt enable             */
#define DS_CTRL_A1IE     0x01u   /* Alarm 1 interrupt enable (disabled)  */
#define DS_CTRL_BBSQW    0x40u   /* battery-backed SQW (keep 0)          */
#define DS_CTRL_EOSC     0x80u   /* 0 = oscillator enabled               */

/* STATUS register bits */
#define DS_STAT_A2F      0x02u   /* Alarm 2 fired flag */
#define DS_STAT_A1F      0x01u   /* Alarm 1 fired flag */
#define DS_STAT_OSF      0x80u   /* oscillator stop flag */

/* Alarm mask bits — set bit 7 of register to ignore that field. */
#define ALARM_MATCH_MIN  0x00u   /* bit 7 clear = compare this field     */
#define ALARM_IGNORE     0x80u   /* bit 7 set   = ignore this field      */

/* ---- BCD helpers --------------------------------------------------------- */
static uint8_t bcd_to_bin(uint8_t bcd) { return (bcd >> 4) * 10u + (bcd & 0x0Fu); }
static uint8_t bin_to_bcd(uint8_t bin) { return ((bin / 10u) << 4) | (bin % 10u); }

/* ---- Public API ---------------------------------------------------------- */

uint8_t ds3231_init(void)
{
    /* INTCN = 1 (alarm drives /INT), BBSQW = 0, EOSC = 0, A1IE = 0, A2IE = 0. */
    return i2c_reg_write(DS3231_ADDR, DS_REG_CONTROL, DS_CTRL_INTCN);
}

uint8_t ds3231_read(rtc_time_t *t)
{
    uint8_t raw[7];
    if (i2c_reg_read(DS3231_ADDR, DS_REG_SECONDS, raw, 7u)) return 1u;

    t->second = bcd_to_bin(raw[0] & 0x7Fu);
    t->minute = bcd_to_bin(raw[1] & 0x7Fu);
    t->hour   = bcd_to_bin(raw[2] & 0x3Fu);   /* 24-hour mode: bit 6 = 0 */
    /* raw[3] = day-of-week, skip */
    t->day    = bcd_to_bin(raw[4] & 0x3Fu);
    t->month  = bcd_to_bin(raw[5] & 0x1Fu);
    t->year   = bcd_to_bin(raw[6]);
    return 0u;
}

uint8_t ds3231_write(const rtc_time_t *t)
{
    uint8_t raw[7];
    raw[0] = bin_to_bcd(t->second);
    raw[1] = bin_to_bcd(t->minute);
    raw[2] = bin_to_bcd(t->hour);    /* 24-hour mode: bit 6 already 0 */
    raw[3] = 0x01u;                  /* day-of-week = 1 (unused)       */
    raw[4] = bin_to_bcd(t->day);
    raw[5] = bin_to_bcd(t->month);
    raw[6] = bin_to_bcd(t->year);

    /* Write all seven time registers in one burst. */
    {
        uint8_t buf[8];
        uint8_t i;
        buf[0] = DS_REG_SECONDS;
        for (i = 0u; i < 7u; i++) buf[i + 1u] = raw[i];
        return i2c_write(DS3231_ADDR, buf, 8u);
    }
}

uint8_t ds3231_set_alarm_minutes(uint8_t n)
{
    uint8_t buf[4];
    uint8_t ctrl;

    /*
     * Alarm 2 has three registers: minute, hour, day/date.
     * To fire every minute: all three mask bits = 1.
     * To fire at a specific minute each hour: minute mask = 0, others = 1.
     */
    if (n == 0u) {
        /* Every minute: all mask bits set. */
        buf[0] = ALARM_IGNORE;
        buf[1] = ALARM_IGNORE;
        buf[2] = ALARM_IGNORE;
    } else {
        buf[0] = bin_to_bcd(n);   /* match minute n */
        buf[1] = ALARM_IGNORE;    /* any hour       */
        buf[2] = ALARM_IGNORE;    /* any day        */
    }

    /* Write Alarm 2 registers starting at 0x0B. */
    {
        uint8_t pkt[4];
        pkt[0] = DS_REG_ALM2_MIN;
        pkt[1] = buf[0];
        pkt[2] = buf[1];
        pkt[3] = buf[2];
        if (i2c_write(DS3231_ADDR, pkt, 4u)) return 1u;
    }

    /* Clear any stale flag, then enable Alarm 2 interrupt. */
    if (ds3231_clear_alarm()) return 1u;

    if (i2c_reg_read(DS3231_ADDR, DS_REG_CONTROL, &ctrl, 1u)) return 1u;
    ctrl |=  DS_CTRL_A2IE;
    ctrl &= ~DS_CTRL_A1IE;
    return i2c_reg_write(DS3231_ADDR, DS_REG_CONTROL, ctrl);
}

uint8_t ds3231_clear_alarm(void)
{
    uint8_t status;
    if (i2c_reg_read(DS3231_ADDR, DS_REG_STATUS, &status, 1u)) return 1u;
    status &= ~(DS_STAT_A2F | DS_STAT_A1F);
    return i2c_reg_write(DS3231_ADDR, DS_REG_STATUS, status);
}
