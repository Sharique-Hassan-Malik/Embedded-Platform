#include "mpu6050.h"
#include <Wire.h>
#include <math.h>

static void write_reg(uint8_t addr, uint8_t reg, uint8_t val)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

static uint8_t read_reg(uint8_t addr, uint8_t reg)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(addr, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0;
}

static void read_burst(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t len)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(addr, len);
    for (uint8_t i = 0; i < len && Wire.available(); i++)
        buf[i] = Wire.read();
}

bool mpu6050_init(uint8_t addr)
{
    Wire.begin();
    Wire.setClock(400000);

    /* Verify device identity. */
    if (read_reg(addr, MPU6050_WHO_AM_I) != 0x68)
        return false;

    /* Wake up, select PLL with X gyro as clock reference (more stable than
     * the internal 8 MHz oscillator). */
    write_reg(addr, MPU6050_PWR_MGMT_1, 0x01);
    delay(10);

    /* Sample rate: 100 Hz.  Internal ODR is 1 kHz with DLPF enabled.
     * SMPLRT_DIV = (1000 / 100) - 1 = 9. */
    write_reg(addr, MPU6050_SMPLRT_DIV, 9);

    /* DLPF_CFG = 3: accel BW 44 Hz, gyro BW 42 Hz.  Reduces vibration noise
     * while keeping enough bandwidth to capture fall transients (~10–20 Hz). */
    write_reg(addr, MPU6050_CONFIG, 0x03);

    /* Gyroscope full scale ±2000 °/s (FS_SEL = 3). */
    write_reg(addr, MPU6050_GYRO_CONFIG, 0x18);

    /* Accelerometer full scale ±16 g (AFS_SEL = 3). */
    write_reg(addr, MPU6050_ACCEL_CONFIG, 0x18);

    /* INT pin: active-low, push-pull, latch until read. */
    write_reg(addr, MPU6050_INT_PIN_CFG, 0x20);

    /* Enable DATA_RDY interrupt. */
    write_reg(addr, MPU6050_INT_ENABLE, 0x01);

    return true;
}

bool mpu6050_read(uint8_t addr, Mpu6050Sample *out)
{
    /* Check DATA_RDY flag. */
    if (!(read_reg(addr, MPU6050_INT_STATUS) & 0x01))
        return false;

    /* Burst-read 14 bytes: ACCEL_XOUT_H … GYRO_ZOUT_L. */
    uint8_t buf[14];
    read_burst(addr, MPU6050_ACCEL_XOUT_H, buf, 14);

    auto to_s16 = [](uint8_t hi, uint8_t lo) -> int16_t {
        return (int16_t)((uint16_t)hi << 8 | lo);
    };

    out->ax   = to_s16(buf[0],  buf[1])  * MPU6050_ACCEL_SCALE;
    out->ay   = to_s16(buf[2],  buf[3])  * MPU6050_ACCEL_SCALE;
    out->az   = to_s16(buf[4],  buf[5])  * MPU6050_ACCEL_SCALE;
    out->temp = to_s16(buf[6],  buf[7])  / 340.0f + 36.53f;
    out->gx   = to_s16(buf[8],  buf[9])  * MPU6050_GYRO_SCALE;
    out->gy   = to_s16(buf[10], buf[11]) * MPU6050_GYRO_SCALE;
    out->gz   = to_s16(buf[12], buf[13]) * MPU6050_GYRO_SCALE;

    return true;
}

bool mpu6050_self_test(uint8_t addr)
{
    /* Enable self-test on all axes (XA_ST, YA_ST, ZA_ST, XG_ST, YG_ST, ZG_ST). */
    write_reg(addr, MPU6050_ACCEL_CONFIG, 0xF8);  /* AFS_SEL=3, all ST enabled */
    write_reg(addr, MPU6050_GYRO_CONFIG,  0xE0);  /* FS_SEL=0, all ST enabled  */
    delay(20);

    uint8_t st_buf[14];
    read_burst(addr, MPU6050_ACCEL_XOUT_H, st_buf, 14);
    int16_t st_ax = (int16_t)((uint16_t)st_buf[0] << 8 | st_buf[1]);
    int16_t st_ay = (int16_t)((uint16_t)st_buf[2] << 8 | st_buf[3]);
    int16_t st_az = (int16_t)((uint16_t)st_buf[4] << 8 | st_buf[5]);
    int16_t st_gx = (int16_t)((uint16_t)st_buf[8] << 8 | st_buf[9]);
    int16_t st_gy = (int16_t)((uint16_t)st_buf[10] << 8 | st_buf[11]);
    int16_t st_gz = (int16_t)((uint16_t)st_buf[12] << 8 | st_buf[13]);

    /* Disable self-test, restore normal config. */
    write_reg(addr, MPU6050_ACCEL_CONFIG, 0x18);
    write_reg(addr, MPU6050_GYRO_CONFIG,  0x18);
    delay(20);

    uint8_t norm_buf[14];
    read_burst(addr, MPU6050_ACCEL_XOUT_H, norm_buf, 14);
    int16_t n_ax = (int16_t)((uint16_t)norm_buf[0] << 8 | norm_buf[1]);
    int16_t n_ay = (int16_t)((uint16_t)norm_buf[2] << 8 | norm_buf[3]);
    int16_t n_az = (int16_t)((uint16_t)norm_buf[4] << 8 | norm_buf[5]);
    int16_t n_gx = (int16_t)((uint16_t)norm_buf[8] << 8 | norm_buf[9]);
    int16_t n_gy = (int16_t)((uint16_t)norm_buf[10] << 8 | norm_buf[11]);
    int16_t n_gz = (int16_t)((uint16_t)norm_buf[12] << 8 | norm_buf[13]);

    /* Self-test response = reading_with_ST - reading_without_ST.
     * Factory trim values are stored in registers 0x0D–0x12 (3-bit codes).
     * For a quick check, verify the response is non-zero (≥ 50 LSB) on each
     * axis, which is sufficient to confirm the MEMS element is functional. */
    bool pass = true;
    pass &= (abs(st_ax - n_ax) > 50);
    pass &= (abs(st_ay - n_ay) > 50);
    pass &= (abs(st_az - n_az) > 50);
    pass &= (abs(st_gx - n_gx) > 50);
    pass &= (abs(st_gy - n_gy) > 50);
    pass &= (abs(st_gz - n_gz) > 50);

    return pass;
}
