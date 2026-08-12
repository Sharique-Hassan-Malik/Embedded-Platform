#include "encoders.h"
#include "midi_protocol.h"

// Quadrature Gray-code transition table.
// Row = previous state (A<<1)|B, column = current state (A<<1)|B.
// Flattened to a 16-element 1-D array: index = (prev<<2)|curr.
//
//        curr: 00  01  11  10
//  prev 00:     0  -1   0  +1
//  prev 01:    +1   0  -1   0
//  prev 11:     0  +1   0  -1
//  prev 10:    -1   0  +1   0
//
// A +1 entry means one clockwise detent; -1 means counter-clockwise.
const int8_t EncoderBank::QUAD_TABLE[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
};

void EncoderBank::begin() {
    for (uint8_t i = 0; i < ENCODER_COUNT; ++i) {
        pinMode(ENC_PINS[i][0], INPUT_PULLUP);
        pinMode(ENC_PINS[i][1], INPUT_PULLUP);
        _prev[i]  = readRaw(i);
        _value[i] = 64;  // start at midpoint
    }
}

void EncoderBank::update() {
    for (uint8_t i = 0; i < ENCODER_COUNT; ++i) {
        uint8_t curr = readRaw(i);

        if (curr != _prev[i]) {
            uint8_t idx   = static_cast<uint8_t>((_prev[i] << 2) | curr);
            int8_t  delta = QUAD_TABLE[idx];

            if (delta != 0) {
                // Clamp to 0–127 without wrapping.
                int16_t next = static_cast<int16_t>(_value[i]) + delta;
                _value[i] = static_cast<uint8_t>(constrain(next, ENCODER_MIN, ENCODER_MAX));
                MIDI::sendControlChange(
                    GLOBAL_CHANNEL,
                    static_cast<uint8_t>(ENCODER_CC_BASE + i),
                    _value[i]
                );
            }
            _prev[i] = curr;
        }
    }
}

// Read current Gray-code state of encoder enc: returns (A<<1)|B.
uint8_t EncoderBank::readRaw(uint8_t enc) const {
    uint8_t a = digitalRead(ENC_PINS[enc][0]) ? 1 : 0;
    uint8_t b = digitalRead(ENC_PINS[enc][1]) ? 1 : 0;
    return static_cast<uint8_t>((a << 1) | b);
}
