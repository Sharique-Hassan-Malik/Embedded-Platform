#include "config.h"
#include "ringbuf.h"
#if USE_INA219
#include "ina219.h"
#endif
#include <Wire.h>

// ── Global state ──────────────────────────────────────────────────────────────
static RingBuffer g_ring;
static volatile bool g_capturing = false;

#if USE_INA219
static INA219 g_ina;
#endif

// Precomputed Timer1 OCR1A for the configured sample rate.
// Timer1 in CTC, prescaler 8: tick = 0.5 µs at 16 MHz.
// OCR1A = F_CPU / (prescaler * SAMPLE_RATE_HZ) - 1
static constexpr uint16_t T1_OCR = (16000000UL / (8UL * SAMPLE_RATE_HZ)) - 1;

// ── Annotation pin state ──────────────────────────────────────────────────────
// Read all annotation pins atomically into a bitmask.
static uint8_t readAnnotations() {
    uint8_t mask = 0;
    for (uint8_t i = 0; i < ANN_PIN_COUNT; ++i) {
        if (digitalRead(ANN_PINS[i]) == HIGH)
            mask |= (1 << i);
    }
    return mask;
}

// ── Timer1 COMPA ISR — fires at SAMPLE_RATE_HZ ───────────────────────────────
ISR(TIMER1_COMPA_vect) {
    if (!g_capturing) return;

    Sample s;

#if USE_INA219
    s.current_0p1mA = g_ina.current_0p1mA();
#else
    // Direct ADC path: 10-bit reading, convert to 0.1 mA units.
    // current_0p1mA = (adc / 1023 * ADC_VREF / ADC_GAIN) / (SHUNT_MOHM/1e6) * 10000
    uint16_t adc = analogRead(AIN_SHUNT);
    float    v   = (adc / 1023.0f) * ADC_VREF / ADC_GAIN;
    s.current_0p1mA = static_cast<int32_t>(v / (SHUNT_MOHM * 1e-6f) * 10.0f);
#endif

    s.ann_mask = readAnnotations();
    g_ring.push(s);
}

// ── Serial packet sender ──────────────────────────────────────────────────────
// Sends one 5-byte sample packet.
// Wire format: 'S' | current_lo | current_hi | ann_mask | checksum
static void sendSample(const Sample &s) {
    // Clamp to uint16_t range for the wire format (0–65535 = 0–6553.5 mA).
    int32_t  clamped = constrain(s.current_0p1mA, 0L, 65535L);
    uint8_t  lo      = static_cast<uint8_t>(clamped & 0xFF);
    uint8_t  hi      = static_cast<uint8_t>((clamped >> 8) & 0xFF);
    uint8_t  ann     = s.ann_mask;
    uint8_t  csum    = 0xFF ^ lo ^ hi ^ ann;

    Serial.write(PKT_SAMPLE);
    Serial.write(lo);
    Serial.write(hi);
    Serial.write(ann);
    Serial.write(csum);
}

// ── Timer1 setup ──────────────────────────────────────────────────────────────
static void startTimer() {
    TCCR1A = 0;
    TCCR1B = (1 << WGM12) | (1 << CS11);  // CTC, prescaler 8
    OCR1A  = T1_OCR;
    TCNT1  = 0;
    TIMSK1 = (1 << OCIE1A);
}

static void stopTimer() {
    TIMSK1 = 0;
    TCCR1B = 0;
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(BAUD_RATE);
    pinMode(STATUS_PIN, OUTPUT);

    for (uint8_t i = 0; i < ANN_PIN_COUNT; ++i)
        pinMode(ANN_PINS[i], INPUT_PULLUP);

#if USE_INA219
    if (!g_ina.begin(INA219_ADDR, SHUNT_MOHM, INA219_PG, INA219_BADC, INA219_SADC)) {
        // Signal wiring error: rapid blink on status LED.
        while (true) {
            digitalWrite(STATUS_PIN, HIGH); delay(100);
            digitalWrite(STATUS_PIN, LOW);  delay(100);
        }
    }
#else
    analogReference(DEFAULT);
    pinMode(AIN_SHUNT, INPUT);
#endif

    startTimer();
}

// ── Main loop ─────────────────────────────────────────────────────────────────
void loop() {
    // ── Handle host commands ──────────────────────────────────────────────────
    if (Serial.available()) {
        uint8_t cmd = static_cast<uint8_t>(Serial.read());
        switch (cmd) {
            case PKT_START:
                g_ring.clear();
                g_ring.clearOverflow();
                g_capturing = true;
                digitalWrite(STATUS_PIN, HIGH);
                break;

            case PKT_STOP:
                g_capturing = false;
                digitalWrite(STATUS_PIN, LOW);
                break;

            case PKT_IDENT:
                // Send sample rate so host can calibrate its time axis.
                Serial.write(PKT_RATE);
                Serial.write(static_cast<uint8_t>(SAMPLE_RATE_HZ & 0xFF));
                Serial.write(static_cast<uint8_t>((SAMPLE_RATE_HZ >> 8) & 0xFF));
                break;

            default:
                break;
        }
    }

    // ── Drain ring buffer to serial ───────────────────────────────────────────
    Sample s;
    // Send up to 32 samples per loop() iteration to prevent starvation.
    uint8_t sent = 0;
    while (sent < 32 && g_ring.pop(&s)) {
        sendSample(s);
        sent++;
    }

    // ── Overflow warning ──────────────────────────────────────────────────────
    if (g_ring.overflow()) {
        g_ring.clearOverflow();
        // Send a single 'O' byte to signal the host that samples were dropped.
        Serial.write('O');
    }
}
