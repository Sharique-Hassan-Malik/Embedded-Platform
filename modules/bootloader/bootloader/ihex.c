#include "ihex.h"
#include "flash.h"
#include "boot_shared.h"

#include <xc.h>
#include <stdint.h>
#include <string.h>

/* ---- UART polled I/O ----------------------------------------------------- */

#define UART_TIMEOUT_MS  2000u

static void uart_putc(uint8_t c)
{
    while (!TXSTAbits.TRMT)
        ;
    TXREG = c;
}

/* Returns received byte or 0xFF on timeout (~2 s at 4 MHz). */
static uint8_t uart_getc(void)
{
    uint32_t t = (uint32_t)UART_TIMEOUT_MS * 400u;   /* ~400 iterations/ms at 4 MHz */
    while (!PIR1bits.RCIF) {
        if (--t == 0u) return 0xFFu;
    }
    if (RCSTAbits.OERR) {
        RCSTAbits.CREN = 0u;
        RCSTAbits.CREN = 1u;
    }
    return RCREG;
}

/* ---- Hex nibble decode --------------------------------------------------- */

static int8_t hex_nibble(uint8_t c)
{
    if (c >= '0' && c <= '9') return (int8_t)(c - '0');
    if (c >= 'A' && c <= 'F') return (int8_t)(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return (int8_t)(c - 'a' + 10);
    return -1;
}

static int8_t hex_byte(uint8_t hi, uint8_t lo, uint8_t *out)
{
    int8_t h = hex_nibble(hi);
    int8_t l = hex_nibble(lo);
    if (h < 0 || l < 0) return -1;
    *out = (uint8_t)((h << 4) | l);
    return 0;
}

/* ---- Row accumulator ----------------------------------------------------- */

/*
 * The PIC18 writes 64-byte rows.  Incoming HEX records may be shorter or span
 * row boundaries.  We buffer data in a 64-byte staging area and flush to flash
 * whenever the address crosses a row boundary.
 */
static uint8_t  row_buf[64];
static uint32_t row_base;    /* base address of current row (row-aligned) */
static uint8_t  row_dirty;   /* 1 = row_buf has uncommitted data */

static void row_flush(void)
{
    if (!row_dirty) return;

    /* Erase then write the row. */
    flash_erase_row(row_base);
    flash_write_row(row_base, row_buf);

    memset(row_buf, 0xFFu, 64u);   /* erased state */
    row_dirty = 0u;
}

static uint8_t row_write_byte(uint32_t addr, uint8_t data)
{
    uint32_t new_base = addr & ~(uint32_t)0x3F;

    /* Reject writes to protected regions. */
    if (addr < APP_START)         return 1u;
    if (addr >= KEY_PAGE_START)   return 1u;

    if (new_base != row_base) {
        row_flush();
        row_base  = new_base;
        /* Pre-fill with 0xFF so unwritten bytes remain erased. */
        flash_read_block(row_base, row_buf, 64u);
    }

    row_buf[addr & 0x3Fu] = data;
    row_dirty = 1u;
    return 0u;
}

/* ---- Record receive and parse -------------------------------------------- */

uint8_t ihex_receive(void)
{
    uint8_t  line[IHEX_BUF_LEN + 2u];  /* + CR + NUL */
    uint8_t  len_idx = 0u;
    uint8_t  c;
    uint16_t seg = 0u;   /* upper 16 bits from EXT_ADDR record */

    /* Initialise row accumulator. */
    row_base  = APP_START;
    row_dirty = 0u;
    flash_read_block(APP_START, row_buf, 64u);

    for (;;) {
        /* Wait for ':' start-of-record marker. */
        do {
            c = uart_getc();
            if (c == 0xFFu) return IHEX_ERROR;
        } while (c != ':');

        /* Read one line up to CR or LF. */
        len_idx = 0u;
        for (;;) {
            c = uart_getc();
            if (c == 0xFFu) { uart_putc(UART_NAK); return IHEX_ERROR; }
            if (c == '\r' || c == '\n') break;
            if (len_idx < IHEX_BUF_LEN)
                line[len_idx++] = c;
        }
        line[len_idx] = '\0';

        /* Need at minimum LL AAAA TT CC = 8 hex chars = 4 bytes → 8 chars. */
        if (len_idx < 8u || (len_idx & 1u)) { uart_putc(UART_NAK); continue; }

        /* Decode bytes from ASCII hex. */
        {
            uint8_t  rec_bytes[IHEX_MAX_DATA_LEN + 5u];
            uint8_t  rec_len_bytes = (uint8_t)(len_idx / 2u);
            uint8_t  i;
            uint8_t  checksum = 0u;

            for (i = 0u; i < rec_len_bytes; i++) {
                if (hex_byte(line[i*2u], line[i*2u+1u], &rec_bytes[i]) < 0) {
                    uart_putc(UART_NAK);
                    goto next_record;
                }
                if (i < rec_len_bytes - 1u)
                    checksum += rec_bytes[i];
            }

            /* Validate checksum: sum of all bytes including length, addr, type
             * and data, then ~sum+1 must equal the last byte. */
            if ((uint8_t)(~checksum + 1u) != rec_bytes[rec_len_bytes - 1u]) {
                uart_putc(UART_NAK);
                goto next_record;
            }

            {
                uint8_t  data_len = rec_bytes[0];
                uint16_t addr16   = ((uint16_t)rec_bytes[1] << 8) | rec_bytes[2];
                uint8_t  rec_type = rec_bytes[3];

                uint32_t full_addr = ((uint32_t)seg << 16) | addr16;

                switch (rec_type) {
                case IHEX_DATA:
                    for (i = 0u; i < data_len; i++) {
                        if (row_write_byte(full_addr + i, rec_bytes[4u + i])) {
                            uart_putc(UART_NAK);
                            goto next_record;
                        }
                    }
                    uart_putc(UART_ACK);
                    break;

                case IHEX_EOF:
                    row_flush();
                    uart_putc(UART_ACK);
                    return IHEX_DONE;

                case IHEX_EXT_ADDR:
                    seg = ((uint16_t)rec_bytes[4] << 8) | rec_bytes[5];
                    uart_putc(UART_ACK);
                    break;

                default:
                    uart_putc(UART_ACK);   /* ignore unsupported record types */
                    break;
                }
            }
        }
        next_record: ;
    }
}
