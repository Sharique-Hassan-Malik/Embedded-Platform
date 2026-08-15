#pragma once
#include <Arduino.h>
#include "config.h"

// Tracks up to ENCODER_COUNT rotary encoders using a Gray-code state machine.
// Each encoder produces CC messages on its assigned CC number when turned.
// No timer or interrupt is required; the state machine is safe to call from loop().
class EncoderBank {
public:
    EncoderBank() = default;

    void begin();
    void update();

private:
    // Packed previous Gray-code state for each encoder (bits: A=1, B=0).
    uint8_t _prev[ENCODER_COUNT] = {};
    // Current CC value for each encoder (0–127).
    uint8_t _value[ENCODER_COUNT] = {};

    // Full quadrature transition table.
    // Index = (prev_state << 2) | curr_state, where state = (A << 1) | B.
    // Value: +1 = CW detent, -1 = CCW detent, 0 = invalid or no movement.
    static const int8_t QUAD_TABLE[16];

    uint8_t readRaw(uint8_t enc) const;
};
