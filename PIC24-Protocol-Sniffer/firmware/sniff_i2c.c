#include "sniff_i2c.h"
#include "hw.h"

#include <xc.h>
#include <stdint.h>

/*
 * sniff_i2c.c — Bit-bang I2C sniffer driven by change-notification interrupts.
 *
 * I2C protocol recap:
 *   START  : SDA falls while SCL is high
 *   STOP   : SDA rises while SCL is high
 *   Data bit is sampled on each rising SCL edge.
 *   9th bit (after 8 data bits) is the ACK (0) or NACK (1) from the receiver.
 *   The first byte after START is the address byte:
 *     bits[7:1] = 7-bit address, bit[0] = R/W (1 = read, 0 = write).
 *
 * State machine:
 *   IDLE        → waiting for START
 *   ADDR_BITS   → collecting 7 address bits + R/W bit
 *   ADDR_ACK    → sampling ACK/NACK after address
 *   DATA_BITS   → collecting 8 data bits
 *   DATA_ACK    → sampling ACK/NACK after data byte
 *
 * Timing budget:
 *   At 400 kHz I2C (fast mode), one bit period = 2.5 µs.
 *   CN ISR entry on PIC24 at 16 MHz takes ≈ 10 cycles = 0.6 µs.
 *   The ISR must complete within one half-bit period (≈ 1.25 µs).
 *   The ISR body is 25–30 cycles; fits comfortably at 400 kHz.
 */

typedef enum {
    S_IDLE = 0,
    S_ADDR_BITS,
    S_ADDR_ACK,
    S_DATA_BITS,
    S_DATA_ACK
} i2c_state_t;

static volatile i2c_state_t state    = S_IDLE;
static volatile uint8_t     bit_cnt  = 0u;
static volatile uint8_t     shift    = 0u;
static volatile uint8_t     is_addr  = 0u;   /* 1 while collecting address byte */

/* Previous pin levels for edge detection. */
static volatile uint8_t     prev_scl = 1u;
static volatile uint8_t     prev_sda = 1u;

void sniff_i2c_init(void)
{
    /* RB8, RB9 as inputs, analogue off. */
    TRISBbits.TRISB8 = 1u;
    TRISBbits.TRISB9 = 1u;



    /* Enable change notification on CN6 (RB8=SCL) and CN7 (RB9=SDA).
     * CNEN1: CN6IE = bit 6, CN7IE = bit 7. */
    CNEN1bits.CN6IE = 1u;
    CNEN1bits.CN7IE = 1u;
    CNPU1bits.CN6PUE = 0u;   /* no pull-up — use external pull-ups on I2C bus */
    CNPU1bits.CN7PUE = 0u;

    IPC4bits.CNIP  = 6u;    /* priority 6 — higher than UART sniffer */
    IFS1bits.CNIF  = 0u;
    IEC1bits.CNIE  = 1u;
}

/* ---- CN interrupt -------------------------------------------------------- */

void __attribute__((interrupt, no_auto_psv)) _CNInterrupt(void)
{
    uint8_t scl = (uint8_t)I2C_SCL_PORT;
    uint8_t sda = (uint8_t)I2C_SDA_PORT;

    /* ---- Detect START and STOP conditions ---- */
    /* START: SDA falls while SCL is high. */
    if (scl && prev_scl && !sda && prev_sda) {
        cbuf_push(((uint32_t)PROTO_I2C << 28) | ((uint32_t)FLAG_I2C_START << 24));
        state   = S_ADDR_BITS;
        bit_cnt = 0u;
        shift   = 0u;
        is_addr = 1u;
        goto done;
    }

    /* STOP: SDA rises while SCL is high. */
    if (scl && prev_scl && sda && !prev_sda) {
        cbuf_push(((uint32_t)PROTO_I2C << 28) | ((uint32_t)FLAG_I2C_STOP << 24));
        state = S_IDLE;
        goto done;
    }

    /* ---- Sample data on rising SCL edge ---- */
    if (scl && !prev_scl) {
        switch (state) {
        case S_ADDR_BITS:
        case S_DATA_BITS:
            shift   = (uint8_t)((shift << 1) | sda);
            bit_cnt++;
            if (bit_cnt == 8u) {
                state   = is_addr ? S_ADDR_ACK : S_DATA_ACK;
                bit_cnt = 0u;
            }
            break;

        case S_ADDR_ACK: {
            uint32_t flags = is_addr ? (uint32_t)FLAG_I2C_ADDR : 0u;
            flags |= sda ? (uint32_t)FLAG_I2C_NACK : (uint32_t)FLAG_I2C_ACK;
            cbuf_push(((uint32_t)PROTO_I2C << 28) | (flags << 24) | shift);
            is_addr = 0u;
            state   = sda ? S_IDLE : S_DATA_BITS;  /* NACK → idle, ACK → data */
            shift   = 0u;
            bit_cnt = 0u;
            break;
        }

        case S_DATA_ACK: {
            uint32_t flags = sda ? (uint32_t)FLAG_I2C_NACK : (uint32_t)FLAG_I2C_ACK;
            cbuf_push(((uint32_t)PROTO_I2C << 28) | (flags << 24) | shift);
            state   = sda ? S_IDLE : S_DATA_BITS;
            shift   = 0u;
            bit_cnt = 0u;
            break;
        }

        default:
            break;
        }
    }

done:
    prev_scl = scl;
    prev_sda = sda;
    IFS1bits.CNIF = 0u;
}
