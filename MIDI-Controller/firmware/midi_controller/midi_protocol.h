#pragma once
#include <Arduino.h>

// All MIDI protocol constants and message encoding are implemented here from
// scratch per the MIDI 1.0 specification and USB MIDI 1.0 class specification.
// The only external dependency is MidiUSB.sendMIDI() / MidiUSB.flush() from the
// MIDIUSB library, used as a raw USB packet transport layer only — analogous to
// using Serial.write() for hardware UART MIDI.

namespace MIDI {

// ── Status byte definitions (MIDI 1.0 spec section 2) ────────────────────────
// High nibble encodes message type; low nibble carries channel (0–15).
static constexpr uint8_t NOTE_OFF         = 0x80;
static constexpr uint8_t NOTE_ON          = 0x90;
static constexpr uint8_t POLY_AFTERTOUCH  = 0xA0;
static constexpr uint8_t CONTROL_CHANGE   = 0xB0;
static constexpr uint8_t PROGRAM_CHANGE   = 0xC0;
static constexpr uint8_t CHAN_PRESSURE     = 0xD0;
static constexpr uint8_t PITCH_BEND       = 0xE0;

// ── USB MIDI Code Index Numbers (CIN) ────────────────────────────────────────
// USB MIDI 1.0 class specification, Table 4-1.
// The CIN occupies the low nibble of the first byte of each 4-byte USB MIDI
// event packet. It tells the host driver how many valid data bytes follow.
static constexpr uint8_t CIN_NOTE_OFF    = 0x08;
static constexpr uint8_t CIN_NOTE_ON     = 0x09;
static constexpr uint8_t CIN_POLY_AT     = 0x0A;
static constexpr uint8_t CIN_CC          = 0x0B;
static constexpr uint8_t CIN_PC          = 0x0C;
static constexpr uint8_t CIN_CHAN_AT     = 0x0D;
static constexpr uint8_t CIN_PITCH_BEND  = 0x0E;

// Cable number occupies the high nibble of the first packet byte.
static constexpr uint8_t CABLE = 0;

// ── Velocity curve ────────────────────────────────────────────────────────────
// Applies a mild square-root curve so light touches still register expressively.
// Input 0–127, output 0–127.
uint8_t applyCurve(uint8_t linear);

// ── Message senders ───────────────────────────────────────────────────────────
void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
void sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity);
void sendControlChange(uint8_t channel, uint8_t cc, uint8_t value);
void sendPitchBend(uint8_t channel, int16_t value);  // -8192 … +8191

// Must be called at the end of each loop() iteration to push the USB MIDI
// packet buffer to the host.
void flush();

} // namespace MIDI
