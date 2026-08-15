#include "config.h"
#include "midi_protocol.h"
#include "pads.h"
#include "encoders.h"
#include "faders.h"

static PadScanner  pads;
static EncoderBank encoders;
static FaderBank   faders;

void setup() {
    pads.begin();
    encoders.begin();
    faders.begin();
}

void loop() {
    pads.update();
    encoders.update();
    faders.update();
    MIDI::flush();
}
