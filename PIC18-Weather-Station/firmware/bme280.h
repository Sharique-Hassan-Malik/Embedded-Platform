#ifndef BME280_H
#define BME280_H

#include <stdint.h>

/*
 * BME280 combined temperature, humidity and pressure sensor driver.
 * Uses hardware I2C (i2c.h).  Applies Bosch's integer compensation formulas
 * from the BME280 datasheet — no floating-point arithmetic.
 *
 * Output fixed-point scaling:
 *   temperature : int32_t in 0.01 °C  (e.g. 2415 = 24.15 °C)
 *   pressure    : uint32_t in Pa      (e.g. 96386 = 963.86 hPa × 100)
 *   humidity    : uint32_t in 1/1024 %RH (>> 10 gives integer %RH)
 *
 * Configuration applied:
 *   Mode        : forced (one-shot) — sensor sleeps between readings
 *   Oversampling: temperature ×2, pressure ×16, humidity ×1
 *   Filter      : IIR coefficient 4 (reduces impulse noise)
 *   Standby     : n/a (forced mode, no standby)
 *
 * Call sequence:
 *   bme280_init()    — read and store trimming coefficients
 *   bme280_read(&r)  — trigger forced measurement, wait, read and compensate
 */

typedef struct {
    int32_t  temperature;   /* 0.01 °C units                  */
    uint32_t pressure;      /* Pa × 100 (divide by 100 for hPa) */
    uint32_t humidity;      /* %RH × 1024 (>> 10 for integer) */
} bme280_result_t;

/* Initialise: verify chip ID, load trimming parameters.
 * Returns 0 on success, non-zero on I2C error or wrong chip ID. */
uint8_t bme280_init(void);

/* Trigger a forced measurement and wait for completion (typ. 9 ms).
 * Populates *r with compensated values.
 * Returns 0 on success, non-zero on I2C error. */
uint8_t bme280_read(bme280_result_t *r);

#endif /* BME280_H */
