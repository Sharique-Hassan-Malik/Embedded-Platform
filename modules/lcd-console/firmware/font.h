#ifndef FONT_H_INCLUDED
#define FONT_H_INCLUDED

#include <stdint.h>

/*
 * 5×7 proportional bitmap font.
 * Coverage: ASCII 0x20 (space) through 0x5F (underscore) — 64 characters.
 *
 * Each entry is 5 bytes, one byte per column.
 * Bit 0 of each byte = top pixel row; bit 6 = bottom pixel row (7 rows used).
 * Bit 7 is always zero.
 *
 * Index: font5x7[c - 0x20]  where 0x20 ≤ c ≤ 0x5F.
 */
extern const uint8_t font5x7[64][5];

#define FONT_W   5   /* glyph width in pixels  */
#define FONT_H   7   /* glyph height in pixels */
#define FONT_GAP 1   /* inter-character spacing  */

#endif /* FONT_H */
