#include "midi_protocol.h"
#include <MIDIUSB.h>  // transport only: sendMIDI() + flush()
#include <math.h>

namespace MIDI {

// Build and enqueue one 4-byte USB MIDI event packet.
// USB MIDI 1.0 spec, section 4: each event packet is exactly 4 bytes —
//   byte 0: (cable_number << 4) | cin
//   byte 1: MIDI status byte
//   byte 2: first data byte (or 0 for messages with fewer data bytes)
//   byte 3: second data byte (or 0)
static void enqueue(uint8_t cin, uint8_t status, uint8_t b1, uint8_t b2) {
    midiEventPacket_t pkt;
    pkt.header = static_cast<uint8_t>((CABLE << 4) | cin);
    pkt.byte1  = status;
    pkt.byte2  = b1;
    pkt.byte3  = b2;
    MidiUSB.sendMIDI(pkt);
}

uint8_t applyCurve(uint8_t linear) {
    // Map 0–127 through sqrt to compress high velocities slightly, giving
    // better resolution at the low end of the pressure range.
    // sqrt(127) ≈ 11.27 → scale factor keeps maximum at 127.
    float normalised = static_cast<float>(linear) / 127.0f;
    float curved     = sqrtf(normalised);
    return static_cast<uint8_t>(curved * 127.0f + 0.5f);
}

void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    // MIDI 1.0: Note On — status 0x9n, data[0] = note 0–127, data[1] = vel 0–127.
    // Velocity 0 on a Note On is equivalent to Note Off per spec; we avoid it
    // by clamping to 1 before this function is called (see pads.cpp).
    enqueue(
        CIN_NOTE_ON,
        static_cast<uint8_t>(NOTE_ON | (channel & 0x0F)),
        note     & 0x7F,
        velocity & 0x7F
    );
}

void sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
    // MIDI 1.0: Note Off — status 0x8n.
    // Release velocity is sent as 64 (neutral) unless the hardware supports it.
    enqueue(
        CIN_NOTE_OFF,
        static_cast<uint8_t>(NOTE_OFF | (channel & 0x0F)),
        note     & 0x7F,
        velocity & 0x7F
    );
}

void sendControlChange(uint8_t channel, uint8_t cc, uint8_t value) {
    // MIDI 1.0: Control Change — status 0xBn, data[0] = cc 0–119, data[1] = value 0–127.
    // CC numbers 120–127 are reserved for channel mode messages.
    enqueue(
        CIN_CC,
        static_cast<uint8_t>(CONTROL_CHANGE | (channel & 0x0F)),
        cc    & 0x7F,
        value & 0x7F
    );
}

void sendPitchBend(uint8_t channel, int16_t value) {
    // MIDI 1.0: Pitch Bend — status 0xEn.
    // Value range: -8192 (full down) to +8191 (full up), center = 0.
    // Encoded as a 14-bit unsigned integer: centre = 0x2000 (8192).
    // Transmitted as two 7-bit bytes: LSB first, then MSB.
    uint16_t bent = static_cast<uint16_t>(constrain(value, -8192, 8191) + 8192);
    enqueue(
        CIN_PITCH_BEND,
        static_cast<uint8_t>(PITCH_BEND | (channel & 0x0F)),
        static_cast<uint8_t>(bent & 0x7F),         // LSB
        static_cast<uint8_t>((bent >> 7) & 0x7F)   // MSB
    );
}

void flush() {
    MidiUSB.flush();
}

} // namespace MIDI
