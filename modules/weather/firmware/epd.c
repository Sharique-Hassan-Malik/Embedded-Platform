#include "epd.h"
#include "spi.h"
#include "hw.h"
#include "font.h"

#include <xc.h>
#include <stdint.h>
#include <string.h>

uint8_t epd_buf[EPD_PAGES][EPD_STRIDE];

/* ---- SSD1680 command bytes ----------------------------------------------- */
#define CMD_DRV_OUTPUT   0x01u
#define CMD_DATA_ENTRY   0x11u
#define CMD_SW_RESET     0x12u
#define CMD_TEMP_SENSOR  0x18u
#define CMD_MASTER_ACT   0x20u
#define CMD_DISP_CTRL1   0x21u
#define CMD_DISP_CTRL2   0x22u
#define CMD_WRITE_RAM_BW 0x24u
#define CMD_BORDER       0x3Cu
#define CMD_SET_RAM_X    0x44u
#define CMD_SET_RAM_Y    0x45u
#define CMD_SET_X_CURS   0x4Eu
#define CMD_SET_Y_CURS   0x4Fu
#define CMD_NOP          0xFFu

/* ---- Low-level command/data send ----------------------------------------- */

static void epd_cmd(uint8_t cmd)
{
    EPD_DC_CMD();
    EPD_CS_LOW();
    spi_byte(cmd);
    EPD_CS_HIGH();
}

static void epd_data1(uint8_t d)
{
    EPD_DC_DATA();
    EPD_CS_LOW();
    spi_byte(d);
    EPD_CS_HIGH();
}

static void epd_data(const uint8_t *buf, uint16_t len)
{
    EPD_DC_DATA();
    EPD_CS_LOW();
    spi_write(buf, len);
    EPD_CS_HIGH();
}

static void wait_busy(void)
{
    while (EPD_IS_BUSY())
        ;
}

/* ---- Initialisation sequence --------------------------------------------- */

void epd_init(void)
{
    /* Hardware reset: hold RST low ≥ 10 ms, then ≥ 10 ms recovery. */
    EPD_RST_LOW();
    __delay_ms(10);
    EPD_RST_HIGH();
    __delay_ms(10);

    wait_busy();
    epd_cmd(CMD_SW_RESET);
    wait_busy();

    /* Gate driving voltage: GS = 0 (MX/MY/GS). */
    epd_cmd(CMD_DRV_OUTPUT);
    epd_data1(0x7Fu);   /* MUX = 127 (128 gates) */
    epd_data1(0x00u);
    epd_data1(0x00u);

    /* Data entry mode: Y decrement, X increment, address counter follows X. */
    epd_cmd(CMD_DATA_ENTRY);
    epd_data1(0x03u);

    /* Set RAM X address range: 0x00 to 0x12 (18 = 37 bytes − 1). */
    epd_cmd(CMD_SET_RAM_X);
    epd_data1(0x00u);
    epd_data1((uint8_t)(EPD_STRIDE - 1u));

    /* Set RAM Y address range: 0x0127 (295) down to 0x0000. */
    epd_cmd(CMD_SET_RAM_Y);
    epd_data1(0x27u);  epd_data1(0x01u);  /* Y start = 295 */
    epd_data1(0x00u);  epd_data1(0x00u);  /* Y end   = 0   */

    /* Use internal temperature sensor. */
    epd_cmd(CMD_TEMP_SENSOR);
    epd_data1(0x80u);

    /* Border waveform: follow LUT. */
    epd_cmd(CMD_BORDER);
    epd_data1(0x05u);

    /* Display update sequence: load temperature + LUT, then display. */
    epd_cmd(CMD_DISP_CTRL2);
    epd_data1(0xF7u);
}

/* ---- Framebuffer helpers ------------------------------------------------- */

void epd_clear(uint8_t colour)
{
    memset(epd_buf, colour, sizeof(epd_buf));
}

void epd_pixel(uint16_t x, uint16_t y, uint8_t pixel)
{
    uint16_t byte_idx;
    uint8_t  bit_idx;
    uint8_t  page;

    if (x >= EPD_W || y >= EPD_H) return;

    /* Display is rotated 90° in memory: physical x maps to RAM row,
     * physical y maps to RAM column (byte and bit within row).
     * Here we treat the buffer as a simple raster:
     * page = y/8, bit = 7 - (x % 8), byte = x / 8. */
    page     = (uint8_t)(y >> 3);
    byte_idx = x >> 3;
    bit_idx  = (uint8_t)(7u - (x & 0x07u));

    if (pixel)
        epd_buf[page][byte_idx] |=  (uint8_t)(1u << bit_idx);
    else
        epd_buf[page][byte_idx] &= ~(uint8_t)(1u << bit_idx);
}

/* ---- Text rendering ------------------------------------------------------ */

void epd_str(uint16_t x, uint16_t y, const char *s, uint8_t pixel)
{
    while (*s) {
        char     c = *s++;
        uint8_t  col, row, byte_val;
        const uint8_t *glyph;

        if (c < 0x20 || c > 0x5F) {
            x += FONT_W + FONT_GAP;
            continue;
        }
        glyph = font5x7[(uint8_t)c - 0x20u];
        for (col = 0u; col < FONT_W; col++) {
            byte_val = glyph[col];
            for (row = 0u; row < FONT_H; row++) {
                if (byte_val & (1u << row))
                    epd_pixel(x + col, y + row, pixel);
            }
        }
        x += FONT_W + FONT_GAP;
    }
}

/* ---- Display transfer ---------------------------------------------------- */

void epd_full_refresh(void)
{
    uint8_t page;

    /* Reset RAM cursor. */
    epd_cmd(CMD_SET_X_CURS);
    epd_data1(0x00u);
    epd_cmd(CMD_SET_Y_CURS);
    epd_data1(0x27u);
    epd_data1(0x01u);

    /* Write B/W RAM row by row. */
    epd_cmd(CMD_WRITE_RAM_BW);
    for (page = 0u; page < EPD_PAGES; page++) {
        epd_data(epd_buf[page], EPD_STRIDE);
    }

    /* Trigger full refresh and wait (~2 s). */
    epd_cmd(CMD_MASTER_ACT);
    wait_busy();
}

void epd_deep_sleep(void)
{
    /* Mode 1: retain RAM contents (allows partial update on next wake).
     * The display holds its image with no power applied to the controller. */
    epd_cmd(0x10u);
    epd_data1(0x01u);
    __delay_ms(1);
}
