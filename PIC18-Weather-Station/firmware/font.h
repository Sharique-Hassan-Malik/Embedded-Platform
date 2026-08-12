#ifndef FONT_H_INCLUDED
#define FONT_H_INCLUDED

#include <stdint.h>

/*
 * 5×7 column-major bitmap font.
 * Covers ASCII 0x20 (space) through 0x5F (underscore) — 64 characters.
 * Each entry is 5 bytes; bit 0 of each byte = topmost pixel row.
 *
 * Identical to the font used in the pic18-lcd-console project.
 */
extern const uint8_t font5x7[64][5];

#define FONT_W   5
#define FONT_H   7
#define FONT_GAP 1

#endif /* FONT_H_INCLUDED */
