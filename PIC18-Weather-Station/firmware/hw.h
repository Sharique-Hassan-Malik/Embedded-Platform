#ifndef HW_H
#define HW_H

/* Active-mode oscillator frequency, shared by all modules for __delay_*(). */
#ifndef _XTAL_FREQ
#define _XTAL_FREQ  1000000UL   /* 1 MHz HFINTOSC */
#endif

/*
 * hw.h — Pin assignments and hardware constants for the PIC18F26K22
 *         low-power weather station.
 *
 * Target : PIC18F26K22
 * Fosc   : 1 MHz (HFINTOSC) during active periods
 *          31 kHz LFINTOSC during deep sleep
 *
 * I2C  (MSSP1) : BME280 + DS3231
 *   RC3  SCL1
 *   RC4  SDA1
 *
 * SPI  (MSSP2) : e-ink display + SD card (shared bus, separate CS)
 *   RB1  SCK2
 *   RB2  SDO2  (MOSI)
 *   RB3  SDI2  (MISO)
 *
 * E-ink control lines:
 *   RA0  EPD_CS   (active low)
 *   RA1  EPD_DC   (0 = command, 1 = data)
 *   RA2  EPD_RST  (active low)
 *   RA3  EPD_BUSY (input, high = busy)
 *
 * SD card:
 *   RA4  SD_CS    (active low)
 *
 * Miscellaneous:
 *   RB0  RTC_INT  (DS3231 /INT output, active low — triggers IOC wake)
 *   RC0  LED      (status indicator, active high)
 *
 * All SPI and I2C pins must have analogue functions disabled (ANSEL = 0).
 */

#include <xc.h>
#include <stdint.h>

/* ---- E-ink ------------------------------------------------------------- */
#define EPD_CS_LAT      LATAbits.LATA0
#define EPD_CS_TRIS     TRISAbits.TRISA0
#define EPD_DC_LAT      LATAbits.LATA1
#define EPD_DC_TRIS     TRISAbits.TRISA1
#define EPD_RST_LAT     LATAbits.LATA2
#define EPD_RST_TRIS    TRISAbits.TRISA2
#define EPD_BUSY_PORT   PORTAbits.RA3
#define EPD_BUSY_TRIS   TRISAbits.TRISA3

#define EPD_CS_LOW()    (EPD_CS_LAT  = 0)
#define EPD_CS_HIGH()   (EPD_CS_LAT  = 1)
#define EPD_DC_CMD()    (EPD_DC_LAT  = 0)
#define EPD_DC_DATA()   (EPD_DC_LAT  = 1)
#define EPD_RST_LOW()   (EPD_RST_LAT = 0)
#define EPD_RST_HIGH()  (EPD_RST_LAT = 1)
#define EPD_IS_BUSY()   (EPD_BUSY_PORT != 0)

/* ---- SD card ----------------------------------------------------------- */
#define SD_CS_LAT       LATAbits.LATA4
#define SD_CS_TRIS      TRISAbits.TRISA4
#define SD_CS_LOW()     (SD_CS_LAT = 0)
#define SD_CS_HIGH()    (SD_CS_LAT = 1)

/* ---- RTC interrupt ----------------------------------------------------- */
#define RTC_INT_TRIS    TRISBbits.TRISB0
#define RTC_INT_PORT    PORTBbits.RB0

/* ---- Status LED -------------------------------------------------------- */
#define LED_LAT         LATCbits.LATC0
#define LED_TRIS        TRISCbits.TRISC0
#define LED_ON()        (LED_LAT = 1)
#define LED_OFF()       (LED_LAT = 0)
#define LED_TOGGLE()    (LED_LAT ^= 1)

/* ---- Sensor I2C addresses (7-bit) ------------------------------------- */
#define BME280_ADDR     0x76u   /* SDO tied to GND → 0x76 */
#define DS3231_ADDR     0x68u

/* ---- Logging configuration -------------------------------------------- */
/*
 * Raw-sector SD log layout.
 * Records are 64-byte fixed-width CSV lines starting at SDLOG_START_SECTOR.
 * The current write sector is stored in PIC18 EEPROM at EEPROM_LOG_SECTOR.
 * Maximum log capacity: SDLOG_MAX_SECTORS × 8 records/sector = 512 000 records
 * at one record per 5 minutes → ~1.8 years before wrap.
 */
#define SDLOG_START_SECTOR   2048u   /* skip MBR and FAT areas                */
#define SDLOG_MAX_SECTORS    64000u  /* 32 MB raw log area                    */
#define SDLOG_BYTES_PER_REC  64u     /* fixed record width (padded with spaces)*/
#define SDLOG_RECS_PER_SEC   8u      /* 512 B / 64 B                          */

/* EEPROM addresses for persistent state */
#define EEPROM_LOG_SECTOR    0x00u   /* uint32_t (4 bytes) at 0x00–0x03       */
#define EEPROM_LOG_REC_IDX   0x04u   /* uint8_t  index within sector (0–7)    */
#define EEPROM_MAGIC         0x05u   /* magic byte: 0xA5 = initialised        */
#define EEPROM_MAGIC_VAL     0xA5u

#endif /* HW_H */
