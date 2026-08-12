#include "pov.h"
#include "config.h"
#include "image.h"
#include "leds.h"
#include <avr/interrupt.h>

// ── Shared state between ISRs and POV namespace ───────────────────────────────
// All variables written by an ISR and read in loop() are declared volatile.
// 32-bit variables on AVR are not atomic; reads from loop() must disable
// interrupts around the 4-byte load.

static volatile uint32_t g_hall_prev_us  = 0;   // micros() at previous Hall pulse
static volatile uint32_t g_period_us     = 0;   // last measured revolution period
static volatile uint8_t  g_col           = 0;   // current column index (0 … NUM_COLS-1)
static volatile bool     g_stable        = false;
static volatile uint32_t g_rev_count     = 0;   // total completed revolutions

static uint8_t  g_image_idx   = 0;              // active image (index into IMAGE_TABLE)
static uint32_t g_last_advance = 0;             // revolution count at last auto-advance

// ── Button state ──────────────────────────────────────────────────────────────
static bool     g_btn_prev     = HIGH;
static uint32_t g_btn_debounce = 0;
static constexpr uint8_t DEBOUNCE_MS = 30;

// ── Timer1 helpers ────────────────────────────────────────────────────────────
// Compute OCR1A for a given column period in microseconds.
// With T1_TICK_US = 0.5 µs: OCR1A = round(period_us / 0.5) - 1
static inline uint16_t periodToOCR(uint32_t col_us) {
    uint32_t ticks = (col_us * 2UL);              // period_us / 0.5
    if (ticks > 65535UL) ticks = 65535UL;
    if (ticks == 0)      ticks = 1;
    return static_cast<uint16_t>(ticks - 1);
}

static void timerStart(uint16_t ocr) {
    TCCR1A = 0;
    TCCR1B = (1 << WGM12) | T1_CS_BITS;  // CTC mode, prescaler 8
    OCR1A  = ocr;
    TCNT1  = 0;
    TIMSK1 = (1 << OCIE1A);              // enable compare-A interrupt
}

static void timerStop() {
    TIMSK1 = 0;
    TCCR1B = 0;
    LEDs::blank();
}

// ── Hall-effect sensor ISR ────────────────────────────────────────────────────
// Fires on the RISING edge of INT0 (one pulse per revolution).
//
// Responsibilities:
//   1. Measure the revolution period from successive Hall pulses.
//   2. Check the period against the speed window; blank the display if out of range.
//   3. Recompute the column period and update Timer1's OCR1A.
//   4. Reset the column counter to 0 so column 0 always aligns to the Hall mark.
ISR(INT0_vect) {
    uint32_t now = micros();
    uint32_t period = now - g_hall_prev_us;
    g_hall_prev_us  = now;

    g_rev_count++;

    if (period < PERIOD_US_MIN || period > PERIOD_US_MAX) {
        // Outside usable RPM window — blank and stop the column timer.
        g_stable    = false;
        g_period_us = 0;
        timerStop();
        return;
    }

    g_stable    = true;
    g_period_us = period;
    g_col       = 0;

    // Reprogram Timer1 for the new column period.
    // Reset TCNT1 to 0 so the first column fires at t=0 from the Hall event.
    uint32_t col_us = period / NUM_COLS;
    uint16_t ocr    = periodToOCR(col_us);

    // Atomic update: interrupts are already disabled inside an ISR.
    OCR1A = ocr;
    TCNT1 = 0;

    // Ensure the timer is running (it may have been stopped by a previous
    // out-of-range period).
    TCCR1B = (1 << WGM12) | T1_CS_BITS;
    TIMSK1 = (1 << OCIE1A);
}

// ── Timer1 compare-A ISR ──────────────────────────────────────────────────────
// Fires NUM_COLS times per revolution. Each call outputs one angular column
// of the current image to the LED shift register.
ISR(TIMER1_COMPA_vect) {
    uint8_t col = g_col;

    if (col >= NUM_COLS) {
        // Guard: past the last column — blank until the next Hall pulse resets.
        LEDs::blank();
        return;
    }

    // Read current image column from PROGMEM and write to LEDs.
    const uint8_t *img = IMAGE_TABLE[g_image_idx];
    LEDs::write(imageColumn(img, col));

    g_col = col + 1;
}

// ── Public interface ──────────────────────────────────────────────────────────
namespace POV {

void begin() {
    // Hall sensor: INT0 on D2, RISING edge.
    pinMode(HALL_PIN, INPUT_PULLUP);
    EICRA |= (1 << ISC01) | (1 << ISC00);  // INT0 = RISING
    EIMSK |= (1 << INT0);

    // Image select button.
    pinMode(BTN_PIN, INPUT_PULLUP);

    LEDs::begin();
}

void update() {
    // ── Button debounce ───────────────────────────────────────────────────────
    bool btn = digitalRead(BTN_PIN);
    if (btn == LOW && g_btn_prev == HIGH) {
        // Falling edge — start debounce timer.
        g_btn_debounce = millis();
    }
    if (btn == LOW && g_btn_prev == LOW) {
        if (millis() - g_btn_debounce >= DEBOUNCE_MS) {
            nextImage();
            // Wait for release.
            while (digitalRead(BTN_PIN) == LOW) {}
        }
    }
    g_btn_prev = btn;

    // ── Auto-advance ──────────────────────────────────────────────────────────
    if (AUTO_ADVANCE_REVS > 0) {
        // Atomic read of g_rev_count (4 bytes on AVR).
        uint32_t revs;
        cli();
        revs = g_rev_count;
        sei();

        if (revs - g_last_advance >= AUTO_ADVANCE_REVS) {
            g_last_advance = revs;
            nextImage();
        }
    }
}

void nextImage() {
    g_image_idx = static_cast<uint8_t>((g_image_idx + 1) % NUM_IMAGES);
}

uint16_t currentRPM() {
    uint32_t period;
    cli();
    period = g_period_us;
    sei();

    if (period == 0) return 0;
    return static_cast<uint16_t>(60000000UL / period);
}

bool isStable() {
    return g_stable;
}

} // namespace POV
