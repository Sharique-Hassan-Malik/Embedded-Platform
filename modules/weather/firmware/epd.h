#ifndef EPD_H_INCLUDED
#define EPD_H_INCLUDED

#include <stdint.h>

/*
 * SSD1680 e-ink display driver for Waveshare 2.9-inch (296 × 128) BWR panel.
 * Only the black-and-white plane is used.
 *
 * Framebuffer layout:
 *   epd_buf[page][byte]
 *   page : 0..15  (16 pages × 8 rows = 128 px height)
 *   byte : 0..36  (37 bytes × 8 px = 296 px width; byte 36 uses 8 of 8 bits)
 *
 *   Bit 7 of each byte = leftmost pixel; bit 0 = rightmost.
 *   1 = white, 0 = black  (SSD1680 convention).
 *
 * EPD_W = 296 pixels wide
 * EPD_H = 128 pixels tall
 *
 * The display performs a full refresh (2–3 s) only when epd_full_refresh()
 * is called.  For deep-sleep power budgets call epd_deep_sleep() after
 * each update; re-initialise with epd_init() on the next wake cycle.
 */

#define EPD_W       296u
#define EPD_H       128u
#define EPD_STRIDE  ((EPD_W + 7u) / 8u)   /* 37 bytes per row */
#define EPD_PAGES   (EPD_H / 8u)           /* 16 pages         */

extern uint8_t epd_buf[EPD_PAGES][EPD_STRIDE];

/* Initialise SSD1680 controller and load LUT for full refresh. */
void epd_init(void);

/* Fill the framebuffer: colour = 0xFF (all white) or 0x00 (all black). */
void epd_clear(uint8_t colour);

/* Set or clear a single pixel.  pixel = 0 → black, 1 → white. */
void epd_pixel(uint16_t x, uint16_t y, uint8_t pixel);

/* Draw a null-terminated ASCII string using the 5×7 font from font5x7[]. */
void epd_str(uint16_t x, uint16_t y, const char *s, uint8_t pixel);

/* Transfer framebuffer to display and trigger a full refresh. */
void epd_full_refresh(void);

/* Put SSD1680 into deep sleep mode (power draw < 1 µA). */
void epd_deep_sleep(void);

#endif /* EPD_H_INCLUDED */
