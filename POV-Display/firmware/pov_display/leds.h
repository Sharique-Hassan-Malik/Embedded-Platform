#pragma once
#include <Arduino.h>

// Drives a 74HC595 8-bit serial-in, parallel-out shift register.
// Three wire interface: DATA, CLOCK, LATCH.
//
// The shift register is clocked MSB-first. Bit 7 of the bitmask corresponds
// to the first LED shifted in (outermost position); bit 0 is the last (innermost).
// Adjust bit order by reversing the byte with reverseBits() if your strip
// is wired the other way around.
namespace LEDs {

void begin();

// Output one 8-bit column bitmask to the LED strip immediately.
// Bit N = 1 → LED N on. Called from Timer1 ISR — must be fast.
void write(uint8_t mask);

// Turn all LEDs off. Called during blanking intervals.
void blank();

} // namespace LEDs
