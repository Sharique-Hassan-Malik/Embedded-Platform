#pragma once
#include <Arduino.h>

// Target: Arduino Uno / Nano (ATmega328P, 16 MHz).
//
// Hardware overview:
//   Hall-effect sensor    → INT0 (D2)     — marks the start of each revolution
//   74HC595 shift register                — drives 8 LEDs on the spinning arm
//     DATA                → D11 (MOSI)
//     CLOCK               → D13 (SCK)
//     LATCH               → D10
//   Image select button   → D4 (INPUT_PULLUP, active low)
//
// Timer1 (16-bit CTC) fires one ISR per display column. The period of each
// column interrupt is recomputed every revolution from the measured Hall period.

// ── Hall sensor ───────────────────────────────────────────────────────────────
static constexpr uint8_t HALL_PIN      = 2;    // INT0 — must be D2 on Uno/Nano
static constexpr uint8_t HALL_TRIGGER  = RISING;

// ── 74HC595 shift register ────────────────────────────────────────────────────
static constexpr uint8_t SR_DATA  = 11;
static constexpr uint8_t SR_CLOCK = 13;
static constexpr uint8_t SR_LATCH = 10;

// ── Image selection button ────────────────────────────────────────────────────
static constexpr uint8_t BTN_PIN = 4;

// ── Display geometry ──────────────────────────────────────────────────────────
// Number of angular columns per full 360° revolution.
// Each column occupies (360 / NUM_COLS)° of arc.
static constexpr uint8_t NUM_COLS = 36;   // 10° per column

// Number of LEDs in the strip (one byte per column, one bit per LED).
static constexpr uint8_t NUM_LEDS = 8;

// ── Speed guard ───────────────────────────────────────────────────────────────
// Display is blanked if the rotor is outside this RPM window to avoid
// rendering corrupted images during spin-up or spin-down.
static constexpr uint16_t RPM_MIN = 200;
static constexpr uint16_t RPM_MAX = 4000;

// Derived: revolution period limits in microseconds.
// period_us = 60_000_000 / RPM
static constexpr uint32_t PERIOD_US_MIN = 60000000UL / RPM_MAX;  // ~15 ms
static constexpr uint32_t PERIOD_US_MAX = 60000000UL / RPM_MIN;  // 300 ms

// ── Timer1 prescaler ─────────────────────────────────────────────────────────
// Prescaler 8 → 1 tick = 0.5 µs at 16 MHz.
// Max representable period = 65535 × 0.5 µs ≈ 32.8 ms.
// With NUM_COLS = 36 and RPM_MIN = 200, col_period ≈ 8.3 ms → 16 600 ticks. OK.
static constexpr uint8_t  T1_PRESCALER   = 8;
static constexpr uint8_t  T1_CS_BITS     = (1 << CS11);         // CS11 = prescaler 8
static constexpr float    T1_TICK_US     = static_cast<float>(T1_PRESCALER) / 16.0f;

// ── Image cycling ─────────────────────────────────────────────────────────────
// Auto-advance the displayed image every N complete revolutions (0 = manual only).
static constexpr uint16_t AUTO_ADVANCE_REVS = 0;
