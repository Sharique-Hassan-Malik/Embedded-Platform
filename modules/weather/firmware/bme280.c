#include "bme280.h"
#include "i2c.h"
#include "hw.h"

#include <stdint.h>

/* ---- Register addresses -------------------------------------------------- */
#define REG_ID          0xD0u
#define REG_RESET       0xE0u
#define REG_CTRL_HUM    0xF2u
#define REG_STATUS      0xF3u
#define REG_CTRL_MEAS   0xF4u
#define REG_CONFIG      0xF5u
#define REG_PRESS_MSB   0xF7u   /* F7..F9 pressure, FA..FC temp, FD..FE hum */

#define REG_CALIB00     0x88u   /* T1..P9 trimming: 0x88–0x9F (26 bytes)    */
#define REG_CALIB26     0xE1u   /* H2..H6 trimming: 0xE1–0xE7  (7 bytes)    */
#define REG_DIG_H1      0xA1u   /* H1 single byte                           */

#define BME280_CHIP_ID  0x60u
#define FORCED_MODE     0x01u   /* osrs_t ×2, osrs_p ×16, mode forced       */

/* ---- Trimming coefficients ----------------------------------------------- */
static struct {
    uint16_t T1;
    int16_t  T2, T3;
    uint16_t P1;
    int16_t  P2, P3, P4, P5, P6, P7, P8, P9;
    uint8_t  H1;
    int16_t  H2;
    uint8_t  H3;
    int16_t  H4, H5;
    int8_t   H6;
} cal;

/* t_fine carries a fine temperature value used by pressure and humidity. */
static int32_t t_fine;

/* ---- Helper: little-endian 16-bit assembly ------------------------------- */
static uint16_t le16u(const uint8_t *b) { return (uint16_t)b[0] | ((uint16_t)b[1] << 8); }
static int16_t  le16s(const uint8_t *b) { return (int16_t)le16u(b); }

/* ---- Trimming load ------------------------------------------------------- */
static uint8_t load_calibration(void)
{
    uint8_t raw[26];
    uint8_t h[7];

    /* Temperature and pressure trimming (0x88–0xA1). */
    if (i2c_reg_read(BME280_ADDR, REG_CALIB00, raw, 26u)) return 1u;

    cal.T1 = le16u(raw + 0);
    cal.T2 = le16s(raw + 2);
    cal.T3 = le16s(raw + 4);
    cal.P1 = le16u(raw + 6);
    cal.P2 = le16s(raw + 8);
    cal.P3 = le16s(raw + 10);
    cal.P4 = le16s(raw + 12);
    cal.P5 = le16s(raw + 14);
    cal.P6 = le16s(raw + 16);
    cal.P7 = le16s(raw + 18);
    cal.P8 = le16s(raw + 20);
    cal.P9 = le16s(raw + 22);
    /* raw[24] unused, raw[25] = H1 stored separately */

    if (i2c_reg_read(BME280_ADDR, REG_DIG_H1, &cal.H1, 1u)) return 1u;

    /* Humidity trimming (0xE1–0xE7). */
    if (i2c_reg_read(BME280_ADDR, REG_CALIB26, h, 7u)) return 1u;

    cal.H2 = le16s(h + 0);
    cal.H3 = h[2];
    /* dig_H4/H5 are signed 12-bit: sign-extend the high byte via int8_t
       (per the Bosch BME280 datasheet) before combining the nibble. */
    cal.H4 = (int16_t)(((int8_t)h[3] << 4) | (h[4] & 0x0F));
    cal.H5 = (int16_t)(((int8_t)h[5] << 4) | (h[4] >> 4));
    cal.H6 = (int8_t)h[6];

    return 0u;
}

/* ---- Integer compensation formulas (verbatim from Bosch datasheet) ------- */

static int32_t compensate_temperature(int32_t adc_T)
{
    int32_t var1, var2;
    var1 = ((adc_T >> 3) - ((int32_t)cal.T1 << 1));
    var1 = (var1 * (int32_t)cal.T2) >> 11;
    var2 = (adc_T >> 4) - (int32_t)cal.T1;
    var2 = (((var2 * var2) >> 12) * (int32_t)cal.T3) >> 14;
    t_fine = var1 + var2;
    return (t_fine * 5 + 128) >> 8;   /* units: 0.01 °C */
}

static uint32_t compensate_pressure(int32_t adc_P)
{
    int64_t var1, var2, p;
    var1 = (int64_t)t_fine - 128000;
    var2 = var1 * var1 * (int64_t)cal.P6;
    var2 = var2 + ((var1 * (int64_t)cal.P5) << 17);
    var2 = var2 + ((int64_t)cal.P4 << 35);
    var1 = ((var1 * var1 * (int64_t)cal.P3) >> 8) + ((var1 * (int64_t)cal.P2) << 12);
    var1 = ((INT64_C(0x800000000000) + var1) * (int64_t)cal.P1) >> 33;
    if (var1 == 0) return 0u;
    p    = 1048576 - adc_P;
    p    = (((p << 31) - var2) * 3125) / var1;
    var1 = ((int64_t)cal.P9 * (p >> 13) * (p >> 13)) >> 25;
    var2 = ((int64_t)cal.P8 * p) >> 19;
    p    = ((p + var1 + var2) >> 8) + ((int64_t)cal.P7 << 4);
    return (uint32_t)p;   /* units: Pa × 256; divide by 256 for Pa */
}

static uint32_t compensate_humidity(int32_t adc_H)
{
    int32_t v;
    v = t_fine - 76800;
    v = (((adc_H << 14) - ((int32_t)cal.H4 << 20) - ((int32_t)cal.H5 * v))
         + 16384) >> 15;
    v = v * (((((((v * (int32_t)cal.H6) >> 10)
         * (((v * (int32_t)cal.H3) >> 11) + 32768)) >> 10) + 2097152)
         * (int32_t)cal.H2 + 8192) >> 14);
    v = v - (((((v >> 15) * (v >> 15)) >> 7) * (int32_t)cal.H1) >> 4);
    if (v < 0)           v = 0;
    if (v > 419430400)   v = 419430400;
    return (uint32_t)(v >> 12);   /* units: %RH × 1024 */
}

/* ---- Public API ---------------------------------------------------------- */

uint8_t bme280_init(void)
{
    uint8_t id;

    if (i2c_reg_read(BME280_ADDR, REG_ID, &id, 1u)) return 1u;
    if (id != BME280_CHIP_ID) return 2u;

    /* Soft reset to clear any leftover state. */
    if (i2c_reg_write(BME280_ADDR, REG_RESET, 0xB6u)) return 1u;
    __delay_ms(3);   /* reset completes in ~2 ms */

    return load_calibration();
}

uint8_t bme280_read(bme280_result_t *r)
{
    uint8_t raw[8];
    int32_t adc_T, adc_P, adc_H;
    uint8_t status;
    uint8_t wait;

    /* osrs_h = 001 (×1 oversampling) */
    if (i2c_reg_write(BME280_ADDR, REG_CTRL_HUM, 0x01u)) return 1u;
    /* IIR filter 4; t_sb irrelevant in forced mode */
    if (i2c_reg_write(BME280_ADDR, REG_CONFIG, 0x0Cu)) return 1u;
    /* osrs_t ×2 (011), osrs_p ×16 (101), mode forced (01) → 0x74 | 0x01 */
    if (i2c_reg_write(BME280_ADDR, REG_CTRL_MEAS, 0x75u)) return 1u;

    /* Wait for measurement to complete (typical 8.7 ms at these settings). */
    for (wait = 20u; wait; wait--) {
        __delay_ms(1);
        if (i2c_reg_read(BME280_ADDR, REG_STATUS, &status, 1u)) return 1u;
        if (!(status & 0x08u)) break;   /* measuring bit cleared */
    }
    if (wait == 0u) return 3u;   /* timeout */

    /* Burst-read 8 data registers: F7..FE */
    if (i2c_reg_read(BME280_ADDR, REG_PRESS_MSB, raw, 8u)) return 1u;

    adc_P = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | (raw[2] >> 4);
    adc_T = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | (raw[5] >> 4);
    adc_H = ((int32_t)raw[6] << 8)  |  (int32_t)raw[7];

    r->temperature = compensate_temperature(adc_T);
    r->pressure    = compensate_pressure(adc_P) >> 8;   /* Pa now */
    r->humidity    = compensate_humidity(adc_H);

    return 0u;
}
