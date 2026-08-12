#pragma once
#include <Arduino.h>

namespace Measurement {

void begin();

// Store the current signal reading as I0 (blank/incident intensity).
// Must be called with a blank cuvette (solvent only, no analyte) inserted.
// Returns the stored I0 ADC count.
uint16_t doBlank();

// Measure dark current with LED off. Returns dark ADC count.
uint16_t doDark();

// Take one averaged reading.
// signal_out    — oversampled ADC count on signal channel
// reference_out — oversampled ADC count on reference channel (0 if not fitted)
// absorbance    — Beer-Lambert absorbance, or NAN if no blank has been stored
void doRead(uint16_t &signal_out, uint16_t &reference_out, float &absorbance);

// Set LED PWM duty cycle (0–255).
void setLEDDuty(uint8_t duty);

// Return true if a cuvette is detected in the holder.
bool cuvettePresent();

// Return stored I0 (0 if no blank has been taken).
uint16_t getI0();

// Return stored dark count.
uint16_t getDark();

// Return current LED duty.
uint8_t getLEDDuty();

} // namespace Measurement
