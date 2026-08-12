#include "hw.h"
#include "sniff_uart.h"

#include <xc.h>
#include <stdint.h>

/*
 * sniff_uart.c — Passive UART capture using UART2 receiver.
 *
 * The sniffer is wired in parallel with the target's TX line (no electrical
 * connection to the target RX).  UART2 RX interrupt fires on each received
 * byte.  Framing errors (invalid stop bit) are flagged in the captured word.
 *
 * Baud rate: set by sniff_uart_init().  Common rates are stored in a small
 * lookup table so the host can switch rates over the control channel.
 */

/*
 * Baud rate register: BRG = (Fcy / (16 × baud)) - 1  (BRGH = 0)
 *                  or BRG = (Fcy / (4  × baud)) - 1  (BRGH = 1)
 * Using BRGH = 1 for better accuracy:
 *   9 600  : BRG = 16 000 000 / (4 × 9 600)   - 1 = 415
 *   19 200 : BRG = 16 000 000 / (4 × 19 200)  - 1 = 207
 *   38 400 : BRG = 16 000 000 / (4 × 38 400)  - 1 = 103
 *   57 600 : BRG = 16 000 000 / (4 × 57 600)  - 1 = 68
 *  115 200 : BRG = 16 000 000 / (4 × 115 200) - 1 = 33
 *  250 000 : BRG = 16 000 000 / (4 × 250 000) - 1 = 15
 *  500 000 : BRG = 16 000 000 / (4 × 500 000) - 1 = 7
 */
static const struct { uint32_t baud; uint16_t brg; } baud_table[] = {
    {   9600UL, 415u },
    {  19200UL, 207u },
    {  38400UL, 103u },
    {  57600UL,  68u },
    { 115200UL,  33u },
    { 250000UL,  15u },
    { 500000UL,   7u },
};
#define BAUD_TABLE_LEN  (sizeof(baud_table) / sizeof(baud_table[0]))

static uint32_t active_baud = 115200UL;

void sniff_uart_init(uint32_t baud)
{
    uint8_t  i;
    uint16_t brg = 33u;   /* default 115 200 */

    for (i = 0u; i < BAUD_TABLE_LEN; i++) {
        if (baud_table[i].baud == baud) {
            brg = baud_table[i].brg;
            break;
        }
    }
    active_baud = baud;

    /* Map UART2 RX to sniff input pin via PPS. */
    /* RPINR19<12:8> = U2RXR = SNIFF_UART_RX_RP */
    __builtin_write_OSCCONL(OSCCON & 0xBFu);   /* unlock PPS */
    RPINR19bits.U2RXR = SNIFF_UART_RX_RP;
    __builtin_write_OSCCONL(OSCCON | 0x40u);   /* lock PPS */

    U2BRG  = brg;
    U2MODE = 0u;
    U2MODEbits.BRGH = 1u;   /* high-speed baud */
    U2MODEbits.UARTEN = 1u;
    U2STA  = 0u;
    U2STAbits.URXISEL = 0u;   /* interrupt on each received byte */

    IPC7bits.U2RXIP = 5u;
    IFS1bits.U2RXIF = 0u;
    IEC1bits.U2RXIE = 1u;
}

void sniff_uart_set_baud(uint32_t baud)
{
    IEC1bits.U2RXIE = 0u;
    U2MODEbits.UARTEN = 0u;
    sniff_uart_init(baud);
}

/* ---- UART2 RX interrupt -------------------------------------------------- */

void __attribute__((interrupt, no_auto_psv)) _U2RXInterrupt(void)
{
    while (U2STAbits.URXDA) {
        uint8_t  data  = U2RXREG;
        uint8_t  ferr  = U2STAbits.FERR ? 1u : 0u;
        uint32_t flags = (uint32_t)(ferr ? FLAG_UART_FRAMING_ERR : 0u);
        uint32_t word  = ((uint32_t)PROTO_UART << 28)
                       | (flags << 24)
                       | (uint32_t)data;
        cbuf_push(word);

        if (U2STAbits.OERR) {
            U2STAbits.OERR = 0u;   /* clear overrun — re-enables receiver */
        }
    }
    IFS1bits.U2RXIF = 0u;
}
