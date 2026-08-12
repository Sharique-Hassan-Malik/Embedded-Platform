/*
 * PIC24FJ64GA002 — UART/I2C/SPI Protocol Sniffer
 *
 * All three sniffers run concurrently.  Captured bytes land in the shared
 * circular buffer (hw.h: capture_buf) from their respective ISRs.  The
 * main loop drains the buffer, adds a Timer1 timestamp, serialises each
 * event as a one-line ASCII frame to the host UART (115 200 baud) and
 * optionally logs it to the SD card.
 *
 * Timer1 runs at 1 MHz (TCY/16) and wraps every 65.5 ms.  A 16-bit
 * overflow counter extends it to a 32-bit microsecond timestamp that
 * wraps after ~71 minutes.  Timestamps are good to ±1 µs relative
 * accuracy between events on the same bus.
 *
 * Control channel (host → device, same UART1 at 115 200 baud):
 *   'B' <hex2>   Set UART sniffer baud: 0=9600 1=19200 2=38400 3=57600
 *                4=115200 5=250000 6=500000
 *   'M' <'0'|'3'> Set SPI mode 0 or mode 3
 *   'S'           Stop logging to SD card
 *   'G'           Go — resume logging to SD card
 *   'R'           Reset timestamp counter
 *
 * Each command is a single-byte opcode followed by optional arguments.
 * The device echoes '>' after each command to confirm receipt.
 */

#include <xc.h>
#include <stdint.h>
#include <string.h>

#include "hw.h"
#include "host_uart.h"
#include "sniff_uart.h"
#include "sniff_i2c.h"
#include "sniff_spi.h"
#include "sdlog.h"

/* ---- Configuration bits (PIC24FJ64GA002) --------------------------------- */
_CONFIG1( JTAGEN_OFF & GCP_OFF & GWRP_OFF & ICS_PGx1 & FWDTEN_OFF )
_CONFIG2( IESO_OFF   & FCKSM_CSDCMD & OSCIOFNC_OFF & POSCMOD_NONE
        & FNOSC_FRCPLL & IOL1WAY_OFF )

/* ---- Timer1 timestamp ---------------------------------------------------- */
/* FCY = 16 MHz.  Timer1 prescaler 1:8 → 2 MHz → does not give nice 1 µs.
 * Use prescaler 1:8 → TMR1 ticks at 2 MHz (0.5 µs/tick).  Multiply by 2
 * to get µs.  Overflow ISR increments the high word. */

#define TMR1_PRESCALE  8u   /* 1:8 → 2 MHz tick */

static volatile uint16_t ts_high = 0u;   /* upper 16 bits of 32-bit counter */

uint32_t timestamp_us(void)
{
    uint16_t hi1, lo, hi2;
    /* Read with guard against TMR1 rollover between reads. */
    do {
        hi1 = ts_high;
        lo  = TMR1;
        hi2 = ts_high;
    } while (hi1 != hi2);
    /* Each tick = 0.5 µs → divide by 2 (right shift). */
    return ((uint32_t)hi1 << 15) | (lo >> 1);
}

void __attribute__((interrupt, no_auto_psv)) _T1Interrupt(void)
{
    ts_high++;
    IFS0bits.T1IF = 0u;
}

static void timer1_init(void)
{
    T1CON   = 0u;
    T1CONbits.TCKPS = 0b01u;   /* 1:8 prescaler */
    PR1     = 0xFFFFu;
    TMR1    = 0u;
    IPC0bits.T1IP = 7u;   /* highest priority — timestamp accuracy */
    IFS0bits.T1IF = 0u;
    IEC0bits.T1IE = 1u;
    T1CONbits.TON = 1u;
}

/* ---- Shared buffer definition -------------------------------------------- */
cbuf_t capture_buf;

/* ---- SD logging flag ----------------------------------------------------- */
static uint8_t sd_logging = 0u;
static uint8_t sd_ok      = 0u;

/* ---- Control command receive --------------------------------------------- */

static char cmd_buf[4];
static uint8_t cmd_idx = 0u;

static void process_control(char c)
{
    static const uint32_t baud_list[] = {
        9600UL, 19200UL, 38400UL, 57600UL, 115200UL, 250000UL, 500000UL
    };

    cmd_buf[cmd_idx++] = c;

    switch (cmd_buf[0]) {
    case 'B':
        if (cmd_idx < 2u) return;
        {
            uint8_t idx = (uint8_t)(cmd_buf[1] - '0');
            if (idx < 7u) sniff_uart_set_baud(baud_list[idx]);
        }
        break;
    case 'M':
        if (cmd_idx < 2u) return;
        sniff_spi_set_mode(cmd_buf[1] == '3' ? SPI_MODE_3 : SPI_MODE_0);
        break;
    case 'S':
        if (sd_ok) { sdlog_flush(); }
        sd_logging = 0u;
        break;
    case 'G':
        sd_logging = sd_ok;
        break;
    case 'R':
        ts_high = 0u; TMR1 = 0u;
        break;
    default:
        cmd_idx = 0u;
        return;
    }

    host_uart_puts(">\n");
    cmd_idx = 0u;
}

/* UART1 RX (control channel from host) */
void __attribute__((interrupt, no_auto_psv)) _U1RXInterrupt(void)
{
    while (U1STAbits.URXDA)
        process_control((char)U1RXREG);
    if (U1STAbits.OERR) { U1STAbits.OERR = 0u; }
    IFS0bits.U1RXIF = 0u;
}

/* ---- Main ---------------------------------------------------------------- */

int main(void)
{
    /* Oscillator: FNOSC_FRCPLL runs the 8 MHz FRC through the fixed PLL to
     * give Fosc = 32 MHz → Fcy = 16 MHz (16 MIPS). The 24FJ64GA002 has no
     * PLLDIV config bit; RCDIV = 0 selects the ÷1 postscaler. */
    CLKDIVbits.RCDIV = 0u;   /* FRC postscaler ÷1 */

    /* All PORTB pins digital. The 24FJ64GA002 configures this via AD1PCFG
     * (1 = digital), not per-pin ANSELx. This is a digital protocol sniffer. */
    AD1PCFG = 0xFFFFu;

    /* Unlock PPS once (some PIC24 silicon requires two separate sequences). */
    __builtin_write_OSCCONL(OSCCON & 0xBFu);
    __builtin_write_OSCCONL(OSCCON | 0x40u);

    /* Enable global interrupts (IPL = 0). */
    __builtin_enable_interrupts();

    /* Status LED. */
    LED_TRIS = 0u; LED_OFF();

    /* Initialise subsystems. */
    host_uart_init();

    /* Enable UART1 RX for control channel. */
    TRISBbits.TRISB3  = 1u;
    __builtin_write_OSCCONL(OSCCON & 0xBFu);
    RPINR18bits.U1RXR = 3u;   /* RP3 = U1RX */
    __builtin_write_OSCCONL(OSCCON | 0x40u);
    U1STAbits.URXISEL = 0u;
    IPC2bits.U1RXIP   = 3u;
    IFS0bits.U1RXIF   = 0u;
    IEC0bits.U1RXIE   = 1u;

    timer1_init();
    sniff_uart_init(115200UL);
    sniff_i2c_init();
    sniff_spi_init(SPI_MODE_0);

    /* Try SD card; non-fatal if absent. */
    if (sdlog_init() == 0u) {
        sd_ok      = 1u;
        sd_logging = 1u;
    }

    host_uart_puts("# PIC24 Protocol Sniffer ready\n");

    for (;;) {
        /* Drain capture buffer. */
        while (!cbuf_empty()) {
            uint32_t w  = cbuf_pop();
            uint32_t ts = timestamp_us();

            host_uart_put_u32_hex(ts);
            host_uart_putc(' ');
            host_uart_print_word(w);

            if (sd_logging)
                sdlog_write(ts, w);
        }

        /* Blink LED every ~500 ms to signal activity. */
        {
            static uint16_t blink_ctr = 0u;
            blink_ctr++;
            if (blink_ctr == 0u) LED_TOGGLE();
        }
    }

    return 0;
}
