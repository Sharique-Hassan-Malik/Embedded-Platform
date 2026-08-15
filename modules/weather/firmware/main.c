/*
 * PIC18F26K22 — Low-Power Weather Station with E-Ink Display
 *
 * Peripherals:
 *   BME280  (I2C 0x76) — temperature, humidity, pressure
 *   DS3231  (I2C 0x68) — RTC, periodic alarm, timestamp
 *   SSD1680 (SPI)      — 2.9-inch e-ink display (296×128)
 *   SD card (SPI)      — raw-sector CSV logging
 *
 * Power cycle (one per WAKE_INTERVAL_MIN minutes):
 *   1. PIC18 wakes from Sleep via DS3231 Alarm 2 /INT on RB0 IOC
 *   2. Fosc switches from 31 kHz LFINTOSC to 1 MHz HFINTOSC
 *   3. LED blinks once to signal wake
 *   4. BME280 forced-mode measurement (~10 ms)
 *   5. DS3231 timestamp read
 *   6. E-ink framebuffer rendered and full refresh triggered (~2.5 s)
 *   7. SD card record written (~150 ms)
 *   8. DS3231 alarm flag cleared, next alarm armed
 *   9. E-ink and SD left with CS deasserted; EPD in deep sleep
 *  10. PIC18 enters Sleep; current < 1 µA (MCU) + ~1.5 µA (DS3231)
 *
 * Active-period average current at 1 MHz / 3.3 V:
 *   MCU: ~0.7 mA × 3 s ≈ 2.1 mC per cycle
 *   EPD: ~26 mA × 2.5 s ≈ 65 mC per cycle
 *   At 5-minute intervals this averages to ~0.4 mA including sleep current.
 *   A 2 000 mAh AA cell pair lasts ~200 days.
 *
 * Display layout (296 × 128 e-ink):
 *
 *  y=4   WEATHER STATION             (header, large text × 2)
 *  y=20  2024-06-15  08:30           (date and time)
 *  y=36  TEMP:  +24.1 C
 *  y=52  HUM:    65 %RH
 *  y=68  PRESS: 963.8 hPa
 *  y=84  --------------------------------
 *  y=92  Last update: 08:30:00
 */

#include <xc.h>
#include <stdint.h>
#include <string.h>

#include "hw.h"
#include "i2c.h"
#include "spi.h"
#include "bme280.h"
#include "ds3231.h"
#include "epd.h"
#include "sdlog.h"
#include "font.h"

/* Measurement interval: DS3231 Alarm 2 fires every N minutes (1–59).
 * Set to 5 for production; set to 1 for testing. */
#define WAKE_INTERVAL_MIN  5u

/* ---- Configuration bits (PIC18F26K22) ------------------------------------ */
#pragma config FOSC   = INTIO67    /* Internal oscillator, RA6/RA7 as I/O  */
#pragma config PLLCFG = OFF
#pragma config PRICLKEN = ON
#pragma config FCMEN  = OFF
#pragma config IESO   = OFF
#pragma config PWRTEN = ON
#pragma config BOREN  = SBORDIS
#pragma config BORV   = 190
#pragma config WDTEN  = OFF
#pragma config WDTPS  = 32768
#pragma config CCP2MX = PORTC1
#pragma config PBADEN = OFF        /* PORTB digital on reset */
#pragma config CCP3MX = PORTB5
#pragma config HFOFST = ON
#pragma config T3CMX  = PORTC0
#pragma config P2BMX  = PORTC0     /* 28-pin 26K22 has no PORTD; P2B unused */
#pragma config MCLRE  = EXTMCLR
#pragma config STVREN = ON
#pragma config LVP    = OFF
#pragma config XINST  = OFF
#pragma config CP0=OFF, CP1=OFF, CP2=OFF, CP3=OFF
#pragma config CPB=OFF, CPD=OFF
#pragma config WRT0=OFF, WRT1=OFF, WRT2=OFF, WRT3=OFF
#pragma config WRTB=OFF, WRTC=OFF, WRTD=OFF
#pragma config EBTR0=OFF, EBTR1=OFF, EBTR2=OFF, EBTR3=OFF, EBTRB=OFF

/* ---- ISR ----------------------------------------------------------------- */

/* Wake-up flag: set by IOC ISR when RTC_INT fires. */
static volatile uint8_t woke;

void __interrupt(high_priority) isr_high(void)
{
    /* INT0 external interrupt on RB0 (DS3231 /INT, active-low falling edge). */
    if (INTCONbits.INT0IF) {
        INTCONbits.INT0IF = 0;
        woke = 1u;
    }
}

/* ---- Oscillator helpers -------------------------------------------------- */

static void osc_1mhz(void)
{
    /* HFINTOSC at 1 MHz: IRCF = 001. */
    OSCCON = 0x12u;   /* SCS = 10 (use INTOSC), IRCF = 001 (1 MHz) */
    while (!OSCCONbits.HFIOFS)   /* wait for oscillator to stabilise */
        ;
}

static void osc_lfintosc(void)
{
    /* 31 kHz LFINTOSC: SCS = 11, IRCF irrelevant. */
    OSCCON = 0x03u;
}

/* ---- Display rendering --------------------------------------------------- */

/* Format a fixed-point value: val is in 0.01 units.
 * Writes "+TT.T" or "-TT.T" into buf (needs at least 6 chars + NUL). */
static void fmt_temp(char *buf, int32_t temp_centideg)
{
    int32_t t = temp_centideg;
    uint8_t i = 0u;
    if (t < 0) { buf[i++] = '-'; t = -t; } else { buf[i++] = '+'; }
    /* Two digits integer part. */
    buf[i++] = (char)('0' + (t / 1000));
    buf[i++] = (char)('0' + (t / 100) % 10);
    buf[i++] = '.';
    buf[i++] = (char)('0' + (t / 10) % 10);
    buf[i]   = '\0';
}

/* Format humidity: val is %RH × 1024.  Writes "HHH" (3 chars + NUL). */
static void fmt_hum(char *buf, uint32_t hum_q10)
{
    uint32_t h = hum_q10 >> 10;
    buf[0] = (char)('0' + h / 100u);
    buf[1] = (char)('0' + (h / 10u) % 10u);
    buf[2] = (char)('0' + h % 10u);
    buf[3] = '\0';
}

/* Format pressure in hPa: val is Pa.  Writes "PPPP.P" (6 chars + NUL). */
static void fmt_press(char *buf, uint32_t pa)
{
    uint32_t hpa_i = pa / 100u;
    uint32_t hpa_f = (pa % 100u) / 10u;
    buf[0] = (char)('0' + hpa_i / 1000u);
    buf[1] = (char)('0' + (hpa_i / 100u) % 10u);
    buf[2] = (char)('0' + (hpa_i / 10u) % 10u);
    buf[3] = (char)('0' + hpa_i % 10u);
    buf[4] = '.';
    buf[5] = (char)('0' + hpa_f);
    buf[6] = '\0';
}

/* Format date: "YYYY-MM-DD" (10 chars + NUL). */
static void fmt_date(char *buf, const rtc_time_t *t)
{
    uint16_t y = 2000u + t->year;
    buf[0] = (char)('0' + y / 1000u);
    buf[1] = (char)('0' + (y / 100u) % 10u);
    buf[2] = (char)('0' + (y / 10u) % 10u);
    buf[3] = (char)('0' + y % 10u);
    buf[4] = '-';
    buf[5] = (char)('0' + t->month / 10u);
    buf[6] = (char)('0' + t->month % 10u);
    buf[7] = '-';
    buf[8] = (char)('0' + t->day / 10u);
    buf[9] = (char)('0' + t->day % 10u);
    buf[10] = '\0';
}

/* Format time: "HH:MM" (5 chars + NUL). */
static void fmt_hhmm(char *buf, const rtc_time_t *t)
{
    buf[0] = (char)('0' + t->hour / 10u);
    buf[1] = (char)('0' + t->hour % 10u);
    buf[2] = ':';
    buf[3] = (char)('0' + t->minute / 10u);
    buf[4] = (char)('0' + t->minute % 10u);
    buf[5] = '\0';
}

/* Format time with seconds: "HH:MM:SS" (8 chars + NUL). */
static void fmt_hhmmss(char *buf, const rtc_time_t *t)
{
    fmt_hhmm(buf, t);
    buf[5] = ':';
    buf[6] = (char)('0' + t->second / 10u);
    buf[7] = (char)('0' + t->second % 10u);
    buf[8] = '\0';
}

static void render_display(const rtc_time_t *t, const bme280_result_t *r)
{
    char buf[16];

    epd_clear(0xFFu);   /* all white */

    epd_str(4,  4,  "WEATHER STATION", 0u);

    fmt_date(buf, t);
    epd_str(4, 20, buf, 0u);
    buf[0] = ' '; buf[1] = ' ';   /* two spaces separator */
    fmt_hhmm(buf + 2, t);
    epd_str(4 + 10 * (FONT_W + FONT_GAP), 20, buf, 0u);

    epd_str(4, 36, "TEMP:", 0u);
    fmt_temp(buf, r->temperature);
    epd_str(4 + 5 * (FONT_W + FONT_GAP) + 4, 36, buf, 0u);
    epd_str(4 + 11 * (FONT_W + FONT_GAP) + 4, 36, "C", 0u);

    epd_str(4, 52, "HUM:", 0u);
    fmt_hum(buf, r->humidity);
    epd_str(4 + 5 * (FONT_W + FONT_GAP) + 4, 52, buf, 0u);
    epd_str(4 + 8 * (FONT_W + FONT_GAP) + 4, 52, "%RH", 0u);

    epd_str(4, 68, "PRESS:", 0u);
    fmt_press(buf, r->pressure);
    epd_str(4 + 6 * (FONT_W + FONT_GAP) + 4, 68, buf, 0u);
    epd_str(4 + 12 * (FONT_W + FONT_GAP) + 4, 68, "hPa", 0u);

    /* Horizontal rule. */
    {
        uint16_t x;
        for (x = 4u; x < 292u; x++) epd_pixel(x, 84u, 0u);
    }

    epd_str(4, 92, "Updated:", 0u);
    fmt_hhmmss(buf, t);
    epd_str(4 + 8 * (FONT_W + FONT_GAP) + 4, 92, buf, 0u);
}

/* ---- Main ---------------------------------------------------------------- */

int main(void)
{
    bme280_result_t reading;
    rtc_time_t      now;
    uint8_t         first_boot = 1u;

    osc_1mhz();

    /* LED output. */
    LED_TRIS = 0;
    LED_OFF();

    i2c_init();
    spi_init();
    bme280_init();
    ds3231_init();
    sdlog_init();

    /* Configure RB0 as input for DS3231 /INT (has external pull-up on DS3231). */
    RTC_INT_TRIS = 1;
    ANSELBbits.ANSB0 = 0;

    /* Set DS3231 Alarm 2 to fire every WAKE_INTERVAL_MIN minutes.
     * On first boot the alarm fires on the next matching minute boundary. */
    ds3231_set_alarm_minutes(WAKE_INTERVAL_MIN);

    /* Enable INT0 on RB0 (falling edge = DS3231 /INT asserted). The 18F26K22
     * has interrupt-on-change only on RB4-RB7; RB0 uses the dedicated INT0
     * edge interrupt, which also wakes the CPU from SLEEP. INT0 is fixed at
     * high priority. */
    INTCON2bits.INTEDG0 = 0;   /* trigger on falling edge */
    INTCONbits.INT0IF   = 0;
    INTCONbits.INT0IE   = 1;
    RCONbits.IPEN    = 1;
    INTCONbits.GIEH  = 1;
    INTCONbits.GIEL  = 1;

    for (;;) {
        if (!first_boot && !woke) {
            /* Switch to 31 kHz LFINTOSC to minimise sleep current. */
            osc_lfintosc();
            SLEEP();
            NOP();
            /* Wake here: DS3231 /INT fell, IOC ISR set woke = 1. */
            osc_1mhz();
        }
        first_boot = 0u;
        woke       = 0u;

        LED_ON();

        /* Read sensors. */
        if (bme280_read(&reading))   { LED_OFF(); continue; }
        if (ds3231_read(&now))       { LED_OFF(); continue; }

        /* Update e-ink display. */
        epd_init();
        render_display(&now, &reading);
        epd_full_refresh();
        epd_deep_sleep();

        /* Log to SD card. */
        sdlog_write(&now, &reading);

        /* Re-arm alarm for the next interval. */
        ds3231_clear_alarm();

        LED_OFF();
    }

    return 0;
}
