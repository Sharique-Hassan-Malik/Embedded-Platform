#ifndef MPU6050_H
#define MPU6050_H

#include <stdint.h>
#include <stdbool.h>

/*
 * MPU-6050 6-axis IMU driver.
 *
 * Communicates over I2C at 400 kHz.  The AD0 pin selects the 7-bit address:
 *   AD0 = 0 → 0x68 (default)
 *   AD0 = 1 → 0x69
 *
 * Register map: InvenSense PS-MPU-6000A-00 Rev 4.2.
 *
 * Configuration used here:
 *   Accelerometer: ±16 g full scale, 1 kHz ODR, DLPF 44 Hz
 *   Gyroscope:     ±2000 °/s full scale, 1 kHz ODR, DLPF 42 Hz
 *   Sample rate:   100 Hz via SMPLRT_DIV = 9 (1000 / (1 + 9) = 100)
 *   FIFO:          disabled (polled read)
 *   INT pin:       active-low, data-ready interrupt on pin 2
 */

#define MPU6050_ADDR_LOW   0x68
#define MPU6050_ADDR_HIGH  0x69

/* Key register addresses. */
#define MPU6050_SMPLRT_DIV   0x19
#define MPU6050_CONFIG       0x1A
#define MPU6050_GYRO_CONFIG  0x1B
#define MPU6050_ACCEL_CONFIG 0x1C
#define MPU6050_INT_PIN_CFG  0x37
#define MPU6050_INT_ENABLE   0x38
#define MPU6050_INT_STATUS   0x3A
#define MPU6050_ACCEL_XOUT_H 0x3B   /* first of 14 contiguous data registers */
#define MPU6050_TEMP_OUT_H   0x41
#define MPU6050_GYRO_XOUT_H  0x43
#define MPU6050_PWR_MGMT_1   0x6B
#define MPU6050_WHO_AM_I     0x75

/* Scaling constants.
 *   Accel ±16 g  → 2048 LSB/g  → 1 LSB = 0.000488 g = 4.788 mm/s²
 *   Gyro ±2000°/s → 16.4 LSB/(°/s) → 1 LSB = 0.06098 °/s
 */
#define MPU6050_ACCEL_SCALE  (1.0f / 2048.0f)    /* g per LSB */
#define MPU6050_GYRO_SCALE   (1.0f / 16.4f)      /* °/s per LSB */
#define GRAVITY_MS2          9.80665f

typedef struct {
    float ax, ay, az;   /* accelerometer, g */
    float gx, gy, gz;   /* gyroscope, °/s */
    float temp;         /* on-chip temperature, °C */
} Mpu6050Sample;

/*
 * Initialise the sensor.  Returns true on success; false if the WHO_AM_I
 * register does not return 0x68 (device absent or wiring fault).
 */
bool mpu6050_init(uint8_t addr);

/*
 * Read one sample.  Returns true if data was available (DATA_READY flag set).
 * If the flag is not set the previous sample values are unchanged.
 */
bool mpu6050_read(uint8_t addr, Mpu6050Sample *out);

/*
 * Self-test: apply factory trim values, verify each axis is within ±14% of
 * the trim response.  Returns true if all six axes pass.
 */
bool mpu6050_self_test(uint8_t addr);

#endif /* MPU6050_H */
