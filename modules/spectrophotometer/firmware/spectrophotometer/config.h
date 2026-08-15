#pragma once
#include <Arduino.h>

// Target: Arduino Uno / Nano (ATmega328P, 16 MHz).
//
// Hardware overview:
//   LED (light source)    → D9  (PWM via Timer1, controllable intensity)
//   Photodiode + TIA      → A0  (transimpedance amplifier output)
//   Reference photodiode  → A1  (optional: same LED, no sample — for ratiometric measurement)
//   Cuvette detect switch → D2  (INPUT_PULLUP; LOW when cuvette is seated)
//   LED enable switch     → D3  (INPUT_PULLUP; LOW = LED off for dark reading)
//   Status LED            → D13 (built-in)
//
// Serial protocol (115200 baud, newline-terminated ASCII):
//   Host sends a command character, firmware responds with a data packet.
//   See protocol.h for the full command/response specification.

// ── ADC ───────────────────────────────────────────────────────────────────────
// Oversampling: read the ADC N times and sum; divide by N for averaged result.
// With OVERSAMPLE_N = 64 the effective resolution is 10 + log2(64)/2 = 13 bits.
static constexpr uint8_t  OVERSAMPLE_N   = 64;
// ADC prescaler: 128 → ADC clock = 125 kHz → 104 µs per conversion.
// 64 conversions = ~6.7 ms per averaged reading. Acceptable for this application.
static constexpr uint8_t  ADC_PRESCALER  = 128;   // ADPS2:0 = 111
static constexpr uint16_t ADC_MAX        = 1023;   // 10-bit full scale

// Settle time after switching the LED on before sampling (ms).
static constexpr uint8_t LED_SETTLE_MS  = 10;

// ── LED (light source) ────────────────────────────────────────────────────────
static constexpr uint8_t LED_PIN        = 9;   // OC1A — Timer1 PWM
// Default PWM duty (0–255). Adjust so photodiode sits in the linear region
// (roughly 40–80% of ADC full scale) with the blank cuvette inserted.
static constexpr uint8_t LED_DUTY_DEFAULT = 180;

// ── Photodiode inputs ─────────────────────────────────────────────────────────
static constexpr uint8_t PD_SIGNAL_PIN = A0;   // sample-path photodiode
static constexpr uint8_t PD_REF_PIN    = A1;   // reference photodiode (optional)

// ── Digital I/O ───────────────────────────────────────────────────────────────
static constexpr uint8_t CUVETTE_PIN   = 2;
static constexpr uint8_t LED_EN_PIN    = 3;
static constexpr uint8_t STATUS_PIN    = 13;

// ── Beer-Lambert constants ────────────────────────────────────────────────────
// Absorbance A = -log10(I / I0) = ε · c · l
// The path length l (cuvette width) is fixed at 10 mm (standard cuvette).
static constexpr float PATH_LENGTH_CM  = 1.0f;   // 10 mm = 1.0 cm

// ── Serial baud rate ──────────────────────────────────────────────────────────
static constexpr uint32_t BAUD_RATE    = 115200;
