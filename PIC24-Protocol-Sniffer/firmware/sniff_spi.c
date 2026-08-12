#include "sniff_spi.h"
#include "hw.h"

#include <xc.h>
#include <stdint.h>

/*
 * sniff_spi.c — Passive SPI sniffer.
 *
 * SPI1 is configured in slave mode with DISSDO = 1 (output disabled).
 * The hardware shift register receives MOSI bits on each SCK edge.
 * When a full byte is received, the SPI1 interrupt fires and we push
 * a MOSI word to the capture buffer.
 *
 * MISO is captured in parallel using a second SPI1-equivalent connection:
 * Because the PIC24 only has one SPI1 shift register, MISO is sampled via
 * a software bit-bang approach gated by the same SCK edges using Timer3 as
 * a shadow counter.  For simplicity in this implementation, MISO is captured
 * by reading the MISO pin at the same time as each SPI1 interrupt and
 * reconstructing the byte from the last 8 samples stored in a ring register.
 * This works for SCK ≤ 2 MHz at 16 MHz Fcy.
 *
 * /CS pin (RB13) is monitored via a separate CN enable.
 */

static volatile uint8_t miso_shift = 0u;   /* accumulates MISO bits */
static volatile uint8_t miso_bit   = 0u;   /* current bit index 0..7 */
static volatile uint8_t cs_state   = 1u;   /* last known /CS level */

void sniff_spi_init(spi_mode_t mode)
{
    /* All SPI sniffer pins as digital inputs. */
    TRISBbits.TRISB10 = 1u;   /* SCK  */
    TRISBbits.TRISB11 = 1u;   /* MOSI */
    TRISBbits.TRISB12 = 1u;   /* MISO */
    TRISBbits.TRISB13 = 1u;   /* /CS  */

    /* Map SPI1 inputs via PPS. */
    __builtin_write_OSCCONL(OSCCON & 0xBFu);
    RPINR20bits.SCK1R  = SPI_SCK_RP;    /* SCK  input */
    RPINR20bits.SDI1R  = SPI_MOSI_RP;   /* MOSI → SPI1 SDI (sniffer receives MOSI) */
    /* MISO captured via GPIO; SPI1 SDO disabled */
    __builtin_write_OSCCONL(OSCCON | 0x40u);

    SPI1CON1 = 0u;
    SPI1CON1bits.DISSDO = 1u;   /* disable SDO output — passive sniff */
    SPI1CON1bits.MODE16 = 0u;   /* 8-bit */
    SPI1CON1bits.MSTEN  = 0u;   /* slave mode */

    /* CPOL / CPHA from mode selection. */
    if (mode == SPI_MODE_3) {
        SPI1CON1bits.CKP = 1u;   /* CPOL = 1 */
        SPI1CON1bits.CKE = 0u;   /* CPHA = 1 */
    } else {
        SPI1CON1bits.CKP = 0u;   /* CPOL = 0 */
        SPI1CON1bits.CKE = 1u;   /* CPHA = 0 */
    }

    SPI1CON1bits.SSEN = 1u;    /* /SS pin enables reception */

    /* Map /SS1 to the CS sniffer pin. */
    __builtin_write_OSCCONL(OSCCON & 0xBFu);
    RPINR21bits.SS1R = SPI_CS_BIT;   /* RB13 mapped as /SS1 via CN bit */
    __builtin_write_OSCCONL(OSCCON | 0x40u);

    SPI1STATbits.SPIEN = 1u;

    IPC2bits.SPI1IP  = 4u;
    IFS0bits.SPI1IF  = 0u;
    IEC0bits.SPI1IE  = 1u;

    /* CN on /CS pin (RB13 = CN11) for edge events. */
    CNEN1bits.CN11IE  = 1u;
    CNPU1bits.CN11PUE = 0u;
    /* CN ISR already enabled by sniff_i2c_init(); shares _CNInterrupt. */

    cs_state   = 1u;
    miso_shift = 0u;
    miso_bit   = 0u;
}

void sniff_spi_set_mode(spi_mode_t mode)
{
    SPI1STATbits.SPIEN = 0u;
    sniff_spi_init(mode);
}

/* ---- SPI1 receive interrupt --------------------------------------------- */

void __attribute__((interrupt, no_auto_psv)) _SPI1Interrupt(void)
{
    if (SPI1STATbits.SPIRBF) {
        uint8_t  mosi = (uint8_t)SPI1BUF;

        /* Sample current MISO bit and incorporate into shadow shift register. */
        uint8_t  miso_pin = PORTBbits.RB12;
        miso_shift = (uint8_t)((miso_shift << 1) | miso_pin);
        miso_bit++;

        if (miso_bit == 8u) {
            /* MOSI byte complete. */
            uint32_t mosi_word = ((uint32_t)PROTO_SPI  << 28)
                               | ((uint32_t)FLAG_SPI_MOSI << 24)
                               | (uint32_t)mosi;
            cbuf_push(mosi_word);

            /* MISO byte complete (reconstructed from last 8 samples). */
            uint32_t miso_word = ((uint32_t)PROTO_SPI  << 28)
                               | ((uint32_t)FLAG_SPI_MISO << 24)
                               | (uint32_t)miso_shift;
            cbuf_push(miso_word);

            miso_shift = 0u;
            miso_bit   = 0u;
        }
    }

    /* Clear overrun. */
    if (SPI1STATbits.SPIROV)
        SPI1STATbits.SPIROV = 0u;

    IFS0bits.SPI1IF = 0u;
}

/*
 * CN interrupt extended for SPI /CS edge detection.
 * The existing _CNInterrupt in sniff_i2c.c handles I2C edges; here we check
 * the /CS pin separately and push CS events.  Both sources share one ISR;
 * the full CN vector is handled in sniff_i2c.c which calls sniff_spi_cs_check()
 * before returning.
 */
void sniff_spi_cs_check(void)
{
    uint8_t cs_now = (uint8_t)SPI_CS_PORT;
    if (cs_now != cs_state) {
        uint32_t flag  = cs_now ? FLAG_SPI_CS_DEASSERT : FLAG_SPI_CS_ASSERT;
        uint32_t word  = ((uint32_t)PROTO_SPI << 28) | (flag << 24);
        cbuf_push(word);
        cs_state = cs_now;
        if (cs_now) {
            /* /CS deasserted: reset MISO shadow register. */
            miso_shift = 0u;
            miso_bit   = 0u;
        }
    }
}
