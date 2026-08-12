#pragma once
#include <Arduino.h>
#include "config.h"

// Oversampled ADC reader.
//
// Averaging N samples reduces the noise floor by sqrt(N) and, per the AVR
// application note AVR121, increases effective resolution by log2(N)/2 bits.
// With N=64 the 10-bit ADC gains 3 extra bits → 13-bit effective resolution.
//
// The returned value is scaled back to the 10-bit range (0–1023) by dividing
// the sum by N, so downstream code stays in familiar units.
namespace ADC_OS {

// Configure the ADC prescaler for maximum accuracy (125 kHz ADC clock).
void begin();

// Take N averaged readings from the given analog pin and return the mean.
// Blocks for approximately N × 104 µs.
uint16_t read(uint8_t pin);

} // namespace ADC_OS
