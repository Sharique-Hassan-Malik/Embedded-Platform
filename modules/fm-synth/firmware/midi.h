#ifndef MIDI_H
#define MIDI_H

#include <stdint.h>

typedef enum {
    MIDI_MSG_NONE = 0,
    MIDI_MSG_NOTE_ON,
    MIDI_MSG_NOTE_OFF,
    MIDI_MSG_CC
} midi_msg_type_t;

typedef struct {
    midi_msg_type_t type;
    uint8_t         channel;
    uint8_t         byte1;   /* note number or CC number */
    uint8_t         byte2;   /* velocity or CC value */
} midi_msg_t;

/* Reset the parser state. */
void midi_init(void);

/*
 * Feed one received byte into the parser.
 * Returns 1 and populates *msg when a complete message is assembled.
 * Returns 0 otherwise.
 * Running status is supported: repeated data bytes re-use the last status byte.
 */
int midi_parse(uint8_t byte, midi_msg_t *msg);

/* Map a 7-bit MIDI CC value (0–127) to Q15 range (0–32 766). */
static inline int32_t midi_cc_to_q15(uint8_t value)
{
    return (int32_t)value * 258;   /* 127 * 258 = 32 766 */
}

/* Scale a 7-bit MIDI CC value (0–127) to [0, max_ms] milliseconds. */
static inline uint32_t midi_cc_to_ms(uint8_t value, uint32_t max_ms)
{
    return ((uint32_t)value * max_ms) / 127u;
}

#endif /* MIDI_H */
