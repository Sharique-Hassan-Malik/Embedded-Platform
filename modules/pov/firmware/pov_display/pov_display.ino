#include "config.h"
#include "leds.h"
#include "pov.h"

// Serial debug: prints RPM and stability once per second.
// Disable for production builds to reclaim ~800 bytes of flash.
#define POV_DEBUG 1

void setup() {
#if POV_DEBUG
    Serial.begin(115200);
    Serial.println(F("POV display starting"));
#endif

    POV::begin();
}

void loop() {
    POV::update();

#if POV_DEBUG
    static uint32_t last_print = 0;
    uint32_t now = millis();
    if (now - last_print >= 1000) {
        last_print = now;
        uint16_t rpm = POV::currentRPM();
        Serial.print(F("RPM: "));
        Serial.print(rpm);
        Serial.print(F("  stable: "));
        Serial.println(POV::isStable() ? F("yes") : F("no"));
    }
#endif
}
