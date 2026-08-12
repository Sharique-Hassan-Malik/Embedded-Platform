#ifndef LCD_H_INCLUDED
#define LCD_H_INCLUDED

#include <stdint.h>

/*
 * KS0108 128×64 monochrome LCD driver.
 *
 * The display contains two KS0108 controller chips: CS1 drives the left
 * 64 columns (0–63) and CS2 drives the right 64 columns (64–127).
 * Each chip manages 8 pages of 8 rows, giving 64 total rows.
 *
 * Pin assignments (PIC18F4550):
 *   PORTD (RD0–RD7) — 8-bit parallel data bus
 *   RE0              — RS   (0 = command, 1 = data)
 *   RE1              — R/W  (0 = write, always driven low here)
 *   RE2              — E    (falling edge latches)
 *   RC0              — CS1  (active high, left half)
 *   RC1              — CS2  (active high, right half)
 *
 * The framebuffer is 8 pages × 128 columns = 1 024 bytes.
 * lcd_flush() writes the entire buffer to both controller halves.
 */

#define LCD_W  128
#define LCD_H   64

/*
 * Framebuffer.  Addressed as fb[page][col].
 * page 0 = top 8 rows; page 7 = bottom 8 rows.
 * Within each byte: bit 0 = topmost row of that page.
 */
extern uint8_t lcd_fb[8][LCD_W];

/* Power on, reset and initialise both KS0108 controllers. */
void lcd_init(void);

/* Zero the framebuffer only (does not touch the display). */
void lcd_clear_fb(void);

/* Write the entire framebuffer to the LCD hardware. */
void lcd_flush(void);

#endif /* LCD_H_INCLUDED */
