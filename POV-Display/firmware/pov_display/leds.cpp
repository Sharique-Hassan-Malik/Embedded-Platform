#include "leds.h"
#include "config.h"

// Port and bit masks for direct port manipulation — significantly faster than
// digitalWrite() and safe to call from an ISR without re-entrancy concerns.
// On the ATmega328P, D10 = PB2, D11 = PB3, D13 = PB5.
#define SR_PORT    PORTB
#define SR_DDR     DDRB
#define SR_DATA_BIT  (1 << PB3)   // D11
#define SR_CLOCK_BIT (1 << PB5)   // D13
#define SR_LATCH_BIT (1 << PB2)   // D10

namespace LEDs {

void begin() {
    SR_DDR |= SR_DATA_BIT | SR_CLOCK_BIT | SR_LATCH_BIT;
    blank();
}

// Shift 8 bits into the 74HC595 MSB-first, then pulse LATCH to transfer
// the shift register contents to the output register.
// Bit-banging at direct port speed: ~160 ns per toggle at 16 MHz, so
// one full byte transfer takes approximately 2.6 µs — well within the
// column period even at maximum RPM.
void write(uint8_t mask) {
    SR_PORT &= ~SR_LATCH_BIT;   // LATCH low — hold outputs during shift

    for (int8_t bit = 7; bit >= 0; --bit) {
        // Set DATA to current bit value.
        if (mask & (1 << bit))
            SR_PORT |=  SR_DATA_BIT;
        else
            SR_PORT &= ~SR_DATA_BIT;

        // Rising edge of CLOCK latches DATA into shift register.
        SR_PORT |=  SR_CLOCK_BIT;
        SR_PORT &= ~SR_CLOCK_BIT;
    }

    SR_PORT |= SR_LATCH_BIT;   // Rising edge of LATCH → outputs update
}

void blank() {
    write(0x00);
}

} // namespace LEDs
