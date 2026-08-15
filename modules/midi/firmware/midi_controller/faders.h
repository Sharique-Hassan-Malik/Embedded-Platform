#pragma once
#include <Arduino.h>
#include "config.h"

// Reads FADER_COUNT linear potentiometers and sends CC messages when a fader
// moves by more than FADER_DEADBAND ADC counts from its last transmitted value.
// The deadband prevents the ADC noise floor from generating constant CC traffic.
class FaderBank {
public:
    FaderBank() = default;

    void begin();
    void update();

private:
    uint16_t _last[FADER_COUNT] = {};  // last ADC reading sent as CC

    uint8_t adcToCC(uint16_t adc) const;
};
