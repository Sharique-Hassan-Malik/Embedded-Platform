#include "midi.h"

#include <string.h>

typedef enum {
    PARSE_IDLE = 0,   /* no status byte yet, or waiting for running-status data1 */
    PARSE_DATA1,      /* status received, waiting for first data byte */
    PARSE_DATA2       /* data1 received, waiting for second data byte */
} parse_state_t;

static struct {
    parse_state_t state;
    uint8_t       status;   /* last status byte (for running status) */
    uint8_t       data1;
} parser;

void midi_init(void)
{
    memset(&parser, 0, sizeof(parser));
}

int midi_parse(uint8_t byte, midi_msg_t *msg)
{
    /* Status byte (bit 7 set) */
    if (byte & 0x80) {
        /* Real-time messages (0xF8–0xFF) are single-byte; ignore silently. */
        if (byte >= 0xF8) return 0;
        /* System common / SysEx — reset and skip. */
        if (byte >= 0xF0) {
            parser.state  = PARSE_IDLE;
            parser.status = 0;
            return 0;
        }
        parser.status = byte;
        parser.state  = PARSE_DATA1;
        return 0;
    }

    /* Data byte — requires a valid status. */
    if (parser.status == 0) return 0;

    switch (parser.state) {

    case PARSE_IDLE:
        /* Running status: treat incoming byte as data1. */
        parser.data1 = byte;
        parser.state = PARSE_DATA2;
        return 0;

    case PARSE_DATA1:
        parser.data1 = byte;
        parser.state = PARSE_DATA2;
        return 0;

    case PARSE_DATA2: {
        uint8_t msg_type = parser.status & 0xF0;
        uint8_t channel  = parser.status & 0x0F;
        uint8_t data2    = byte;

        /* Ready for next running-status message. */
        parser.state = PARSE_IDLE;

        msg->channel = channel;
        msg->byte1   = parser.data1;
        msg->byte2   = data2;

        if (msg_type == 0x90 && data2 > 0) {
            msg->type = MIDI_MSG_NOTE_ON;
            return 1;
        }
        if (msg_type == 0x80 || (msg_type == 0x90 && data2 == 0)) {
            msg->type = MIDI_MSG_NOTE_OFF;
            return 1;
        }
        if (msg_type == 0xB0) {
            msg->type = MIDI_MSG_CC;
            return 1;
        }
        return 0;
    }

    default:
        parser.state = PARSE_IDLE;
        return 0;
    }
}
