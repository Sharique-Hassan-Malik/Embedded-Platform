#include "host_uart.h"
#include "hw.h"

#include <xc.h>
#include <stdint.h>

void host_uart_init(void)
{
    TRISBbits.TRISB2 = 0u;


    __builtin_write_OSCCONL(OSCCON & 0xBFu);
    RPOR1bits.RP2R = 3u;   /* RP2 = U1TX */
    __builtin_write_OSCCONL(OSCCON | 0x40u);

    /* 115 200 baud: BRG = (16 000 000 / (4 × 115 200)) - 1 = 33 */
    U1BRG           = 33u;
    U1MODE          = 0u;
    U1MODEbits.BRGH = 1u;
    U1MODEbits.UARTEN = 1u;
    U1STA           = 0u;
    U1STAbits.UTXEN = 1u;
}

void host_uart_putc(char c)
{
    while (U1STAbits.UTXBF);
    U1TXREG = (uint8_t)c;
}

void host_uart_puts(const char *s)
{
    while (*s) host_uart_putc(*s++);
}

void host_uart_put_hex(uint8_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    host_uart_putc(hex[v >> 4]);
    host_uart_putc(hex[v & 0xFu]);
}

void host_uart_put_u32_hex(uint32_t v)
{
    host_uart_put_hex((uint8_t)(v >> 24));
    host_uart_put_hex((uint8_t)(v >> 16));
    host_uart_put_hex((uint8_t)(v >>  8));
    host_uart_put_hex((uint8_t)(v));
}

/* ---- Frame decoder ------------------------------------------------------- */

void host_uart_print_word(uint32_t w)
{
    uint8_t proto = (uint8_t)(w >> 28);
    uint8_t flags = (uint8_t)((w >> 24) & 0x0Fu);
    uint8_t data  = (uint8_t)(w & 0xFFu);

    switch (proto) {

    case PROTO_UART:
        host_uart_puts("U ");
        host_uart_put_hex(data);
        if (flags & FLAG_UART_FRAMING_ERR)
            host_uart_puts(" FE");
        host_uart_putc('\n');
        break;

    case PROTO_I2C:
        host_uart_putc('I');
        host_uart_putc(' ');
        if (flags & FLAG_I2C_START) {
            host_uart_puts("S\n");
        } else if (flags & FLAG_I2C_STOP) {
            host_uart_puts("P\n");
        } else if (flags & FLAG_I2C_ADDR) {
            host_uart_puts("A ");
            host_uart_put_hex((uint8_t)(data >> 1));   /* 7-bit address */
            host_uart_putc(' ');
            host_uart_putc((data & 0x01u) ? 'R' : 'W');
            host_uart_putc(' ');
            host_uart_puts((flags & FLAG_I2C_NACK) ? "NAK" : "ACK");
            host_uart_putc('\n');
        } else {
            /* Data byte. */
            host_uart_puts("D ");
            host_uart_put_hex(data);
            host_uart_putc(' ');
            host_uart_puts((flags & FLAG_I2C_NACK) ? "NAK" : "ACK");
            host_uart_putc('\n');
        }
        break;

    case PROTO_SPI:
        host_uart_putc('S');
        host_uart_putc(' ');
        if (flags & FLAG_SPI_CS_ASSERT) {
            host_uart_puts("CS\n");
        } else if (flags & FLAG_SPI_CS_DEASSERT) {
            host_uart_puts("CD\n");
        } else if (flags & FLAG_SPI_MOSI) {
            host_uart_puts("O ");
            host_uart_put_hex(data);
            host_uart_putc('\n');
        } else if (flags & FLAG_SPI_MISO) {
            host_uart_puts("I ");
            host_uart_put_hex(data);
            host_uart_putc('\n');
        }
        break;

    default:
        break;
    }
}
