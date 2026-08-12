#pragma once
#include <Arduino.h>

// Target: Arduino Leonardo (ATmega32U4).
// USB MIDI transport: MIDIUSB library (raw USB event packets only).
// MIDI protocol logic: implemented from scratch in midi_protocol.h/.cpp.

// ── MIDI ─────────────────────────────────────────────────────────────────────
static constexpr uint8_t GLOBAL_CHANNEL = 0;     // 0 = Ch 1 … 15 = Ch 16

// ── Pads ─────────────────────────────────────────────────────────────────────
// 8 FSR pads read through a 74HC4051 8-to-1 analog multiplexer.
static constexpr uint8_t  PAD_COUNT      = 8;
static constexpr uint8_t  PAD_NOTE_BASE  = 36;   // C2 — pad 0 is C2, pad 1 is C#2 …
static constexpr uint8_t  PAD_VEL_MIN    = 20;
static constexpr uint8_t  PAD_VEL_MAX    = 127;
static constexpr uint16_t PAD_THRESH_ON  = 80;   // ADC count (0–1023) to arm
static constexpr uint16_t PAD_THRESH_OFF = 40;   // hysteresis release level
static constexpr uint8_t  PAD_PEAK_MS    = 20;   // ms to track peak after arm

// 74HC4051 multiplexer wiring
static constexpr uint8_t MUX_COM = A0;           // common I/O → Leonardo A0
static constexpr uint8_t MUX_S0  = 4;
static constexpr uint8_t MUX_S1  = 11;
static constexpr uint8_t MUX_S2  = 12;

// ── Encoders ─────────────────────────────────────────────────────────────────
static constexpr uint8_t ENCODER_COUNT   = 4;
static constexpr uint8_t ENCODER_CC_BASE = 16;   // CC 16–19
static constexpr uint8_t ENCODER_MIN     = 0;
static constexpr uint8_t ENCODER_MAX     = 127;

// [index][0 = A pin, 1 = B pin]
static const uint8_t ENC_PINS[ENCODER_COUNT][2] = {
    {2,  3},
    {5,  6},
    {7,  8},
    {9, 10}
};

// ── Faders ────────────────────────────────────────────────────────────────────
static constexpr uint8_t FADER_COUNT    = 4;
static constexpr uint8_t FADER_CC_BASE  = 20;    // CC 20–23
static constexpr uint8_t FADER_DEADBAND = 3;     // ADC units — suppress noise

static const uint8_t FADER_PINS[FADER_COUNT] = {A1, A2, A3, A4};

// ── LED feedback (optional) ───────────────────────────────────────────────────
// Set to 255 to disable a pad's LED.
static const uint8_t LED_PINS[PAD_COUNT] = {13, 255, 255, 255, 255, 255, 255, 255};
