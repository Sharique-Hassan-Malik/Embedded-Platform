#include "adc_os.h"

namespace ADC_OS {

void begin() {
    // Set ADC prescaler to 128: ADPS2=1, ADPS1=1, ADPS0=1.
    // ADC clock = 16 MHz / 128 = 125 kHz — within the 50–200 kHz spec for
    // maximum resolution on the ATmega328P.
    ADCSRA = (ADCSRA & ~0x07) | 0x07;
}

uint16_t read(uint8_t pin) {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < OVERSAMPLE_N; ++i) {
        sum += static_cast<uint32_t>(analogRead(pin));
    }
    // Divide by N to return a value scaled to the original 10-bit range.
    return static_cast<uint16_t>(sum / OVERSAMPLE_N);
}

} // namespace ADC_OS
