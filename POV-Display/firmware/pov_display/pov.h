#pragma once
#include <Arduino.h>

// POV display engine.
//
// Two interrupt sources cooperate to render a stable mid-air image:
//
//   INT0  (Hall sensor, RISING)
//         Fires once per revolution. Records the current timestamp, computes
//         the revolution period from the previous Hall event, reprograms
//         Timer1's compare value for the new column period and resets the
//         column counter to 0.
//
//   TIMER1 COMPA
//         Fires NUM_COLS times per revolution (once per angular column).
//         Reads the current image column from flash and writes it to the
//         LED shift register.
//
// The column period is updated atomically each revolution, so the display
// continuously adapts to speed changes without accumulating drift.

namespace POV {

void begin();

// Call once per loop() iteration to handle non-ISR tasks:
//   — image auto-advance after AUTO_ADVANCE_REVS revolutions
//   — button debounce for manual image advance
void update();

// Advance to the next image in IMAGE_TABLE. Safe to call from loop().
void nextImage();

// Return the most recently measured RPM (approximate, updated each revolution).
uint16_t currentRPM();

// True if the rotor is within the RPM window defined by RPM_MIN / RPM_MAX.
bool isStable();

} // namespace POV
