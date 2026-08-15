#include "gfx.h"
#include "lcd.h"
#include "font.h"

#include <string.h>

/* ---- pixel ---------------------------------------------------------------- */
void gfx_pixel(int8_t x, int8_t y, uint8_t on)
{
    uint8_t page, bit;

    if (x < 0 || x >= LCD_W || y < 0 || y >= LCD_H)
        return;

    page = (uint8_t)y >> 3;            /* y / 8 */
    bit  = (uint8_t)y & 0x07;          /* y % 8 */

    if (on)
        lcd_fb[page][(uint8_t)x] |=  (uint8_t)(1u << bit);
    else
        lcd_fb[page][(uint8_t)x] &= ~(uint8_t)(1u << bit);
}

/* ---- filled rectangle ----------------------------------------------------- */
void gfx_fill_rect(int8_t x, int8_t y, uint8_t w, uint8_t h, uint8_t on)
{
    /* Use 16-bit locals throughout: a full-width span (w = LCD_W = 128) makes
       the right edge reach x = 127, which would overflow an int8_t loop
       counter (127 -> -128) and spin forever. */
    int16_t cx, cy;
    int16_t x0 = x, y0 = y;
    int16_t x1 = (int16_t)x + (int16_t)w - 1;
    int16_t y1 = (int16_t)y + (int16_t)h - 1;

    /* Clip */
    if (x0 < 0)        x0 = 0;
    if (y0 < 0)        y0 = 0;
    if (x1 >= LCD_W)   x1 = LCD_W - 1;
    if (y1 >= LCD_H)   y1 = LCD_H - 1;
    if (x0 > x1 || y0 > y1) return;

    for (cy = y0; cy <= y1; cy++)
        for (cx = x0; cx <= x1; cx++)
            gfx_pixel((int8_t)cx, (int8_t)cy, on);
}

/* ---- hollow rectangle ----------------------------------------------------- */
void gfx_rect(int8_t x, int8_t y, uint8_t w, uint8_t h, uint8_t on)
{
    gfx_hline(x,                    y,                    w, on);   /* top    */
    gfx_hline(x,                    (int8_t)(y + h - 1),  w, on);   /* bottom */
    gfx_fill_rect(x,                y, 1, h, on);                   /* left   */
    gfx_fill_rect((int8_t)(x + w - 1), y, 1, h, on);               /* right  */
}

/* ---- horizontal line ------------------------------------------------------ */
void gfx_hline(int8_t x, int8_t y, uint8_t len, uint8_t on)
{
    gfx_fill_rect(x, y, len, 1, on);
}

/* ---- character ------------------------------------------------------------ */
int8_t gfx_char(int8_t x, int8_t y, char c, uint8_t on)
{
    uint8_t col, row, byte;
    const uint8_t *glyph;

    if (c < 0x20 || c > 0x5F)
        return x + FONT_W + FONT_GAP;

    glyph = font5x7[(uint8_t)c - 0x20];

    for (col = 0; col < FONT_W; col++) {
        byte = glyph[col];
        for (row = 0; row < FONT_H; row++) {
            if (byte & (1u << row))
                gfx_pixel((int8_t)(x + col), (int8_t)(y + row), on);
        }
    }

    return (int8_t)(x + FONT_W + FONT_GAP);
}

/* ---- string --------------------------------------------------------------- */
int8_t gfx_str(int8_t x, int8_t y, const char *s, uint8_t on)
{
    while (*s)
        x = gfx_char(x, y, *s++, on);
    return x;
}

/* ---- unsigned integer ----------------------------------------------------- */
int8_t gfx_uint(int8_t x, int8_t y, uint16_t value, uint8_t on)
{
    char buf[6];
    uint8_t i = 5;

    buf[i] = '\0';
    if (value == 0) {
        buf[--i] = '0';
    } else {
        while (value > 0 && i > 0) {
            buf[--i] = (char)('0' + value % 10);
            value   /= 10;
        }
    }

    return gfx_str(x, y, &buf[i], on);
}

/* ---- zero-padded integer -------------------------------------------------- */
int8_t gfx_uint_pad(int8_t x, int8_t y, uint16_t value, uint8_t width, uint8_t on)
{
    char    buf[6];
    uint8_t i;

    if (width > 5) width = 5;
    buf[width] = '\0';

    for (i = width; i > 0; i--) {
        buf[i - 1] = (char)('0' + value % 10);
        value     /= 10;
    }

    return gfx_str(x, y, buf, on);
}
