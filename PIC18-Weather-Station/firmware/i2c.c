#include "i2c.h"

#include <xc.h>
#include <stdint.h>

/* ---- internal helpers ---------------------------------------------------- */

static uint8_t wait_idle(void)
{
    uint8_t t = I2C_TIMEOUT;
    while ((SSPCON2 & 0x1Fu) || SSPSTATbits.R_nW) {
        if (--t == 0u) return I2C_ERR_TIMEOUT;
    }
    return 0u;
}

static uint8_t send_byte(uint8_t data)
{
    uint8_t err;
    SSPBUF = data;
    err    = wait_idle();
    if (err) return err;
    return SSPCON2bits.ACKSTAT ? I2C_ERR_NACK : 0u;
}

static uint8_t recv_byte(uint8_t *out, uint8_t ack)
{
    uint8_t t = I2C_TIMEOUT;
    /* Initiate receive. */
    SSPCON2bits.RCEN = 1u;
    while (!SSPSTATbits.BF) {
        if (--t == 0u) return I2C_ERR_TIMEOUT;
    }
    *out = SSPBUF;
    /* Send ACK (0) or NACK (1) for last byte. */
    SSPCON2bits.ACKDT = ack ? 1u : 0u;
    SSPCON2bits.ACKEN = 1u;
    return wait_idle();
}

/* ---- public API ---------------------------------------------------------- */

void i2c_init(void)
{
    /* RC3 = SCL1, RC4 = SDA1 — inputs, analogue off. */
    TRISCbits.TRISC3  = 1;
    TRISCbits.TRISC4  = 1;
    ANSELCbits.ANSC3  = 0;
    ANSELCbits.ANSC4  = 0;

    SSPCON1 = 0x28u;   /* SSPEN = 1, SSPM = 1000 (I2C master)     */
    SSPCON2 = 0x00u;
    SSPSTAT = 0x00u;
    /* SSPADD = (Fosc / (4 × 100 kHz)) − 1 = 1 at 1 MHz */
    SSPADD  = 1u;
}

uint8_t i2c_write(uint8_t addr, const uint8_t *buf, uint8_t len)
{
    uint8_t i, err;

    /* START */
    SSPCON2bits.SEN = 1u;
    if ((err = wait_idle())) return err;

    /* Address + W */
    if ((err = send_byte((uint8_t)(addr << 1)))) goto stop;

    for (i = 0u; i < len; i++) {
        if ((err = send_byte(buf[i]))) goto stop;
    }

stop:
    SSPCON2bits.PEN = 1u;
    wait_idle();
    return err;
}

uint8_t i2c_read(uint8_t addr, uint8_t *buf, uint8_t len)
{
    uint8_t i, err;

    SSPCON2bits.SEN = 1u;
    if ((err = wait_idle())) return err;

    /* Address + R */
    if ((err = send_byte((uint8_t)((addr << 1) | 0x01u)))) goto stop;

    for (i = 0u; i < len; i++) {
        uint8_t is_last = (i == len - 1u);
        if ((err = recv_byte(&buf[i], is_last))) goto stop;
    }

stop:
    SSPCON2bits.PEN = 1u;
    wait_idle();
    return err;
}

uint8_t i2c_reg_read(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t err;

    /* START + address + W + register */
    SSPCON2bits.SEN = 1u;
    if ((err = wait_idle())) return err;
    if ((err = send_byte((uint8_t)(addr << 1)))) goto stop;
    if ((err = send_byte(reg)))                  goto stop;

    /* Repeated START */
    SSPCON2bits.RSEN = 1u;
    if ((err = wait_idle())) goto stop;

    /* Address + R then receive */
    if ((err = send_byte((uint8_t)((addr << 1) | 0x01u)))) goto stop;

    {
        uint8_t i;
        for (i = 0u; i < len; i++) {
            uint8_t is_last = (i == len - 1u);
            if ((err = recv_byte(&buf[i], is_last))) goto stop;
        }
    }

stop:
    SSPCON2bits.PEN = 1u;
    wait_idle();
    return err;
}

uint8_t i2c_reg_write(uint8_t addr, uint8_t reg, uint8_t data)
{
    uint8_t buf[2] = { reg, data };
    return i2c_write(addr, buf, 2u);
}
