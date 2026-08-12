#include "pads.h"
#include "midi_protocol.h"

void PadScanner::begin() {
    pinMode(MUX_S0, OUTPUT);
    pinMode(MUX_S1, OUTPUT);
    pinMode(MUX_S2, OUTPUT);
    pinMode(MUX_COM, INPUT);

    for (uint8_t i = 0; i < PAD_COUNT; ++i) {
        if (LED_PINS[i] != 255) {
            pinMode(LED_PINS[i], OUTPUT);
            digitalWrite(LED_PINS[i], LOW);
        }
    }
}

void PadScanner::update() {
    uint32_t now = millis();

    for (uint8_t i = 0; i < PAD_COUNT; ++i) {
        uint16_t adc = readPad(i);

        switch (_state[i]) {
            case State::IDLE:
                if (adc >= PAD_THRESH_ON) {
                    _state[i]   = State::DETECTING;
                    _peak[i]    = adc;
                    _armTime[i] = now;
                }
                break;

            case State::DETECTING:
                // Track peak ADC during the attack window.
                if (adc > _peak[i]) _peak[i] = adc;

                if (now - _armTime[i] >= PAD_PEAK_MS) {
                    // Attack window closed — commit NoteOn.
                    uint8_t vel = adcToVelocity(_peak[i]);
                    // Velocity 0 on a NoteOn is spec-defined as NoteOff; clamp to 1.
                    if (vel == 0) vel = 1;
                    MIDI::sendNoteOn(GLOBAL_CHANNEL, PAD_NOTE_BASE + i, vel);
                    setLed(i, true);
                    _state[i] = State::ACTIVE;
                }
                break;

            case State::ACTIVE:
                if (adc < PAD_THRESH_OFF) {
                    MIDI::sendNoteOff(GLOBAL_CHANNEL, PAD_NOTE_BASE + i, 64);
                    setLed(i, false);
                    _state[i] = State::IDLE;
                }
                break;
        }
    }
}

// Drive the 74HC4051 address lines to select channel ch (0–7).
void PadScanner::selectMuxChannel(uint8_t ch) const {
    digitalWrite(MUX_S0, (ch >> 0) & 1);
    digitalWrite(MUX_S1, (ch >> 1) & 1);
    digitalWrite(MUX_S2, (ch >> 2) & 1);
    delayMicroseconds(2);  // MUX propagation delay before ADC settle
}

uint16_t PadScanner::readPad(uint8_t pad) const {
    selectMuxChannel(pad);
    return static_cast<uint16_t>(analogRead(MUX_COM));
}

void PadScanner::setLed(uint8_t pad, bool on) const {
    if (LED_PINS[pad] != 255)
        digitalWrite(LED_PINS[pad], on ? HIGH : LOW);
}

// Map peak ADC reading (PAD_THRESH_ON–1023) to MIDI velocity (PAD_VEL_MIN–127)
// and apply the velocity curve from midi_protocol.
uint8_t PadScanner::adcToVelocity(uint16_t adc) const {
    uint16_t clamped = constrain(adc, PAD_THRESH_ON, 1023);
    uint8_t  linear  = static_cast<uint8_t>(
        map(clamped, PAD_THRESH_ON, 1023, PAD_VEL_MIN, PAD_VEL_MAX)
    );
    return MIDI::applyCurve(linear);
}
