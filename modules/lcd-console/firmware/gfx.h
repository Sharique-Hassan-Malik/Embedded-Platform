#ifndef GFX_H
#define GFX_H

#include <stdint.h>

/*
 * Software rendering layer.  All functions operate on lcd_fb[][].
 * Call lcd_flush() to transfer the framebuffer to the physical display.
 *
 * Coordinate system:
 *   x : 0 = left edge,  127 = right edge
 *   y : 0 = top edge,    63 = bottom edge
 *
 * Out-of-bounds writes are silently clipped.
 */

/* Set or clear a single pixel. */
void gfx_pixel(int8_t x, int8_t y, uint8_t on);

/* Draw a filled rectangle. */
void gfx_fill_rect(int8_t x, int8_t y, uint8_t w, uint8_t h, uint8_t on);

/* Draw a hollow rectangle (1px border). */
void gfx_rect(int8_t x, int8_t y, uint8_t w, uint8_t h, uint8_t on);

/* Draw a horizontal line (1px tall). */
void gfx_hline(int8_t x, int8_t y, uint8_t len, uint8_t on);

/*
 * Draw a single ASCII character using the 5×7 font.
 * Characters outside 0x20–0x5F are skipped.
 * Returns the x coordinate immediately to the right of the glyph.
 */
int8_t gfx_char(int8_t x, int8_t y, char c, uint8_t on);

/*
 * Draw a null-terminated ASCII string.
 * Returns the x coordinate immediately after the last character.
 */
int8_t gfx_str(int8_t x, int8_t y, const char *s, uint8_t on);

/*
 * Draw a decimal integer (no leading zeros, no sign).
 * Digits only — caller formats prefix and suffix.
 * Returns the x coordinate after the last digit.
 */
int8_t gfx_uint(int8_t x, int8_t y, uint16_t value, uint8_t on);

/*
 * Draw a decimal integer with exactly 'width' digits, zero-padded.
 * Returns the x coordinate after the last digit.
 */
int8_t gfx_uint_pad(int8_t x, int8_t y, uint16_t value, uint8_t width, uint8_t on);

#endif /* GFX_H */
