#ifndef I2C_H
#define I2C_H

#include <stdint.h>

/*
 * Hardware I2C master driver using MSSP1 on PIC18F26K22.
 * Bit rate: 100 kHz at Fosc = 1 MHz.
 *   SSPADD = (Fosc / (4 × 100 kHz)) − 1 = 1 000 000 / 400 000 − 1 = 1
 *
 * All functions are blocking.  Return value 0 = success, non-zero = error.
 * Errors: I2C_ERR_NACK (slave did not acknowledge), I2C_ERR_TIMEOUT.
 */

#define I2C_ERR_NACK     1u
#define I2C_ERR_TIMEOUT  2u

/* Timeout in poll iterations (~200 µs at 1 MHz). */
#define I2C_TIMEOUT  200u

/* Initialise MSSP1 as I2C master at 100 kHz. */
void i2c_init(void);

/*
 * Write `len` bytes from `buf` to the 7-bit address `addr`.
 * Issues START, address+W, data bytes, STOP.
 */
uint8_t i2c_write(uint8_t addr, const uint8_t *buf, uint8_t len);

/*
 * Read `len` bytes from the 7-bit address `addr` into `buf`.
 * Issues START, address+R, reads with ACK, last byte with NACK, STOP.
 */
uint8_t i2c_read(uint8_t addr, uint8_t *buf, uint8_t len);

/*
 * Register read: write one byte (register address) then repeated-start read.
 * Equivalent to i2c_write(addr, &reg, 1) + i2c_read(addr, buf, len) but
 * using a repeated start instead of a stop between the two phases.
 */
uint8_t i2c_reg_read(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t len);

/* Write a single byte to a register. */
uint8_t i2c_reg_write(uint8_t addr, uint8_t reg, uint8_t data);

#endif /* I2C_H */
