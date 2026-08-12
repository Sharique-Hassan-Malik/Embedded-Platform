#pragma once
#include <avr/pgmspace.h>
#include <Arduino.h>
#include "config.h"

// Each image is a 2-D array: [NUM_COLS] columns, each column is a uint8_t
// bitmask for the 8 LEDs. Bit 0 = LED 0 (innermost / top of strip), bit 7 =
// LED 7 (outermost / bottom of strip). A set bit means the LED is ON.
//
// Images are stored in program flash (PROGMEM) to keep SRAM free — each image
// is 36 bytes; three images = 108 bytes in flash vs. 108 bytes in SRAM.

// ── Helper: retrieve one column from a PROGMEM image array ───────────────────
inline uint8_t imageColumn(const uint8_t *pgm_img, uint8_t col) {
    return pgm_read_byte(pgm_img + col);
}

// ── Image 0: Smiley face ──────────────────────────────────────────────────────
//
// Viewed top-to-bottom (bit 0 … bit 7), left-to-right (col 0 … col 35):
//
//  col:  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 …
//  LED0: .  .  .  X  X  X  X  X  X  .  .  .  .  .  .  .  .  X  X  X  X …
//  LED1: .  .  X  .  .  .  .  .  .  X  .  .  X  .  .  .  X  .  .  .  .
//  LED2: .  X  .  .  .  .  .  .  .  .  X  .  .  X  .  .  .  .  .  .  .
//  LED3: .  X  .  .  .  X  .  .  X  .  X  .  .  X  .  .  .  .  .  X  .
//  LED4: .  X  .  .  .  X  .  .  X  .  X  .  .  X  .  .  .  .  .  X  .
//  LED5: .  X  .  .  .  .  .  .  .  .  X  .  .  X  .  .  .  .  .  .  .
//  LED6: .  .  X  .  .  .  .  .  .  X  .  .  X  .  .  .  X  .  .  .  .
//  LED7: .  .  .  X  X  X  X  X  X  .  .  .  .  .  .  .  .  X  X  X  X …
//
// (Simplified schematic — actual bitmasks below encode a recognisable smiley.)

static const uint8_t IMG_SMILEY[NUM_COLS] PROGMEM = {
    // Outer ring (top + bottom border: bits 0 and 7 on most columns)
    0x00,  // col  0 — gap before face
    0x81,  // col  1 — 10000001 — ring left edge
    0x81,  // col  2
    0xBD,  // col  3 — 10111101
    0xFF,  // col  4 — 11111111 — ring top/bottom fill
    0xFF,  // col  5
    0x81,  // col  6
    0x81,  // col  7
    0x81,  // col  8
    0xFF,  // col  9 — eye left (full column)
    0xFF,  // col 10
    0x81,  // col 11
    0x81,  // col 12
    0xFF,  // col 13 — eye right
    0xFF,  // col 14
    0x81,  // col 15
    0x81,  // col 16
    0x81,  // col 17
    0x81,  // col 18
    0xBD,  // col 19 — smile corner
    0x7E,  // col 20 — 01111110 — smile arc (LEDs 1–6)
    0x3C,  // col 21 — 00111100 — smile centre narrowing
    0x3C,  // col 22
    0x7E,  // col 23
    0xBD,  // col 24 — smile corner
    0x81,  // col 25
    0x81,  // col 26
    0x81,  // col 27
    0x81,  // col 28
    0xBD,  // col 29
    0xFF,  // col 30
    0xFF,  // col 31
    0x81,  // col 32
    0x81,  // col 33
    0x81,  // col 34
    0x00,  // col 35 — gap after face
};

// ── Image 1: Right-pointing arrow ────────────────────────────────────────────
static const uint8_t IMG_ARROW[NUM_COLS] PROGMEM = {
    0x00,  // col  0
    0x00,  // col  1
    0x18,  // col  2 — 00011000 — shaft centre
    0x18,  // col  3
    0x18,  // col  4
    0x18,  // col  5
    0x18,  // col  6
    0x18,  // col  7
    0x18,  // col  8
    0x18,  // col  9
    0x18,  // col 10
    0x18,  // col 11
    0x18,  // col 12
    0x18,  // col 13
    0x18,  // col 14
    0x18,  // col 15
    0x18,  // col 16
    0x18,  // col 17
    0x18,  // col 18
    0x3C,  // col 19 — 00111100 — arrowhead widens
    0x7E,  // col 20 — 01111110
    0xFF,  // col 21 — 11111111 — arrowhead tip
    0x7E,  // col 22 — 01111110
    0x3C,  // col 23 — 00111100
    0x18,  // col 24
    0x00,  // col 25
    0x00,  // col 26
    0x00,  // col 27
    0x00,  // col 28
    0x00,  // col 29
    0x00,  // col 30
    0x00,  // col 31
    0x00,  // col 32
    0x00,  // col 33
    0x00,  // col 34
    0x00,  // col 35
};

// ── Image 2: Letter "P" (for POV) ────────────────────────────────────────────
static const uint8_t IMG_LETTER_P[NUM_COLS] PROGMEM = {
    0x00,  // col  0
    0x00,  // col  1
    0x00,  // col  2
    0xFF,  // col  3 — 11111111 — vertical stroke
    0xFF,  // col  4
    0x19,  // col  5 — 00011001 — top of bowl (bits 0, 3, 4)
    0x19,  // col  6
    0x19,  // col  7
    0x19,  // col  8 — right side of bowl
    0x19,  // col  9
    0x1F,  // col 10 — 00011111 — bottom of bowl closes
    0x1E,  // col 11 — 00011110
    0x18,  // col 12 — 00011000 — back to shaft
    0x18,  // col 13
    0x18,  // col 14
    0x00,  // col 15
    0x00,  // col 16
    0x00,  // col 17
    0x00,  // col 18
    0x00,  // col 19
    0x00,  // col 20
    0x00,  // col 21
    0x00,  // col 22
    0x00,  // col 23
    0x00,  // col 24
    0x00,  // col 25
    0x00,  // col 26
    0x00,  // col 27
    0x00,  // col 28
    0x00,  // col 29
    0x00,  // col 30
    0x00,  // col 31
    0x00,  // col 32
    0x00,  // col 33
    0x00,  // col 34
    0x00,  // col 35
};

// ── Image table ───────────────────────────────────────────────────────────────
static constexpr uint8_t NUM_IMAGES = 3;

// Pointers to each image array in flash. Cast through uintptr_t to avoid
// PROGMEM decay to non-const pointer warnings on avr-gcc.
static const uint8_t * const IMAGE_TABLE[NUM_IMAGES] = {
    IMG_SMILEY,
    IMG_ARROW,
    IMG_LETTER_P,
};
