#pragma once
#include <Arduino.h>
#include "config.h"

// Scans 8 FSR pads multiplexed through a 74HC4051 8-to-1 analog MUX.
// Velocity is derived from the peak ADC reading during the attack window
// (PAD_PEAK_MS ms after the pressure first exceeds PAD_THRESH_ON).
class PadScanner {
public:
    PadScanner() = default;

    void begin();
    void update();

private:
    enum class State : uint8_t { IDLE, DETECTING, ACTIVE };

    State    _state[PAD_COUNT]    = {};
    uint16_t _peak[PAD_COUNT]     = {};
    uint32_t _armTime[PAD_COUNT]  = {};  // millis() when pad first armed

    void     selectMuxChannel(uint8_t ch) const;
    uint16_t readPad(uint8_t pad) const;
    void     setLed(uint8_t pad, bool on) const;
    uint8_t  adcToVelocity(uint16_t adc) const;
};
