#ifndef DS3231_H
#define DS3231_H

#include <stdint.h>

/*
 * DS3231 precision RTC driver.
 * Uses hardware I2C (i2c.h).  BCD registers are converted to/from binary.
 *
 * The RTC is used for two purposes:
 *   1. Timestamping log records.
 *   2. Generating a periodic alarm (Alarm 2) that wakes the PIC18 from Sleep.
 *      Alarm 2 can fire every minute or at a specific minute past each hour.
 *
 * The DS3231 /INT pin (RTC_INT in hw.h) is open-drain active low.
 * It is connected to RB0 which is configured for interrupt-on-change.
 */

typedef struct {
    uint8_t second;   /* 0–59    */
    uint8_t minute;   /* 0–59    */
    uint8_t hour;     /* 0–23    */
    uint8_t day;      /* 1–31    */
    uint8_t month;    /* 1–12    */
    uint8_t year;     /* 0–99 (2000-based) */
} rtc_time_t;

/* Initialise: enable oscillator, disable 32 kHz output, configure INT/SQW
 * for alarm interrupt (not square-wave) mode. */
uint8_t ds3231_init(void);

/* Read the current time into *t. */
uint8_t ds3231_read(rtc_time_t *t);

/* Write time.  Call once to set the initial time. */
uint8_t ds3231_write(const rtc_time_t *t);

/*
 * Set Alarm 2 to fire every N minutes past the hour (0 ≤ n ≤ 59).
 * Setting n = 0 fires every minute.
 * Clears any previous alarm flag and enables the alarm interrupt.
 */
uint8_t ds3231_set_alarm_minutes(uint8_t n);

/* Clear the Alarm 2 interrupt flag (must be called before next alarm fires). */
uint8_t ds3231_clear_alarm(void);

#endif /* DS3231_H */
