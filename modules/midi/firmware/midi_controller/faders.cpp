#include "faders.h"
#include "midi_protocol.h"

void FaderBank::begin() {
    for (uint8_t i = 0; i < FADER_COUNT; ++i) {
        pinMode(FADER_PINS[i], INPUT);
        _last[i] = static_cast<uint16_t>(analogRead(FADER_PINS[i]));
    }
}

void FaderBank::update() {
    for (uint8_t i = 0; i < FADER_COUNT; ++i) {
        uint16_t adc = static_cast<uint16_t>(analogRead(FADER_PINS[i]));

        // Only transmit if the fader has moved beyond the deadband.
        // Using absolute difference avoids signed arithmetic on uint16_t.
        uint16_t diff = (adc > _last[i]) ? (adc - _last[i]) : (_last[i] - adc);

        if (diff > FADER_DEADBAND) {
            MIDI::sendControlChange(
                GLOBAL_CHANNEL,
                static_cast<uint8_t>(FADER_CC_BASE + i),
                adcToCC(adc)
            );
            _last[i] = adc;
        }
    }
}

// Map 10-bit ADC reading (0–1023) to 7-bit CC value (0–127).
uint8_t FaderBank::adcToCC(uint16_t adc) const {
    return static_cast<uint8_t>(adc >> 3);  // divide by 8: 1024/8 = 128
}
