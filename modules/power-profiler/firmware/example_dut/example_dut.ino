// example_dut/example_dut.ino
//
// Example Device Under Test (DUT) sketch demonstrating how to annotate
// code sections for the power profiler.
//
// Connect the DUT's annotation pins to the profiler Arduino's ANN_PINS:
//   DUT D2 → Profiler D2  (annotation channel 0)
//   DUT D3 → Profiler D3  (annotation channel 1)
//   DUT GND → Profiler GND  (common ground — essential)
//
// The profiler captures a rising edge on D2 as the start of section 0
// and a falling edge as its end. The Python dashboard renders the section
// as a coloured span and computes mean/peak/RMS/energy statistics.

// ── Annotation macros ─────────────────────────────────────────────────────────
#define ANN0_ON()   digitalWrite(2, HIGH)
#define ANN0_OFF()  digitalWrite(2, LOW)
#define ANN1_ON()   digitalWrite(3, HIGH)
#define ANN1_OFF()  digitalWrite(3, LOW)

void setup() {
    pinMode(2, OUTPUT);
    pinMode(3, OUTPUT);
    digitalWrite(2, LOW);
    digitalWrite(3, LOW);

    // Also wire the DUT's power rail through the profiler's shunt resistor
    // so that power consumption is measured correctly.
}

void loop() {
    // ── Section 0: ADC burst read ─────────────────────────────────────────────
    ANN0_ON();
    for (uint8_t i = 0; i < 64; ++i) {
        analogRead(A0);
    }
    ANN0_OFF();

    delay(50);

    // ── Section 1: SPI write ──────────────────────────────────────────────────
    ANN1_ON();
    // (SPI.transfer calls would go here)
    delayMicroseconds(500);
    ANN1_OFF();

    delay(50);

    // ── Unlabelled idle ───────────────────────────────────────────────────────
    // Current consumed here appears in the trace but is not assigned to any
    // annotation section. This is the baseline "sleep" current.
    delay(200);
}
