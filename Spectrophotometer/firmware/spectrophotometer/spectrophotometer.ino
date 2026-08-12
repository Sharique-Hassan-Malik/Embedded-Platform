#include "config.h"
#include "protocol.h"
#include "measurement.h"
#include <math.h>

// Send a DATA packet over Serial.
static void sendData(uint16_t signal, uint16_t reference, float absorbance) {
    Serial.print(F("DATA,"));
    Serial.print(signal);
    Serial.print(',');
    Serial.print(reference);
    Serial.print(',');
    if (isnan(absorbance))
        Serial.println(F("nan"));
    else
        Serial.println(absorbance, 4);
}

static void sendACK(const __FlashStringHelper *key, uint32_t value) {
    Serial.print(F("ACK,"));
    Serial.print(key);
    Serial.print(',');
    Serial.println(value);
}

static void sendError(const __FlashStringHelper *reason) {
    Serial.print(F("ERR,"));
    Serial.println(reason);
}

// Parse a 3-digit decimal string from Serial (blocking, 500 ms timeout).
// Returns -1 on timeout or invalid input.
static int16_t readDecimal3() {
    char buf[4] = {};
    uint8_t n   = 0;
    uint32_t deadline = millis() + 500;
    while (n < 3 && millis() < deadline) {
        if (Serial.available()) {
            char c = static_cast<char>(Serial.read());
            if (c == '\n' || c == '\r') break;
            if (c >= '0' && c <= '9') buf[n++] = c;
            else return -1;
        }
    }
    if (n == 0) return -1;
    return static_cast<int16_t>(atoi(buf));
}

void setup() {
    Serial.begin(BAUD_RATE);
    Measurement::begin();
    Serial.println(F("READY"));
}

void loop() {
    if (!Serial.available()) return;

    char cmd = static_cast<char>(Serial.read());

    switch (cmd) {
        case CMD_READ: {
            uint16_t sig, ref;
            float    abs_val;
            Measurement::doRead(sig, ref, abs_val);
            sendData(sig, ref, abs_val);
            break;
        }
        case CMD_BLANK: {
            uint16_t i0 = Measurement::doBlank();
            sendACK(F("I0"), i0);
            break;
        }
        case CMD_DARK: {
            uint16_t dark = Measurement::doDark();
            sendACK(F("DARK"), dark);
            break;
        }
        case CMD_LED: {
            int16_t duty = readDecimal3();
            if (duty < 0 || duty > 255) {
                sendError(F("invalid duty"));
            } else {
                Measurement::setLEDDuty(static_cast<uint8_t>(duty));
                sendACK(F("DUTY"), static_cast<uint8_t>(duty));
            }
            break;
        }
        case CMD_STATUS: {
            Serial.print(F("STATUS,cuvette="));
            Serial.print(Measurement::cuvettePresent() ? 1 : 0);
            Serial.print(F(",duty="));
            Serial.print(Measurement::getLEDDuty());
            Serial.print(F(",I0="));
            Serial.print(Measurement::getI0());
            Serial.print(F(",dark="));
            Serial.println(Measurement::getDark());
            break;
        }
        case CMD_IDENT:
            Serial.println(FIRMWARE_VERSION);
            break;
        case '\n':
        case '\r':
            break;  // ignore bare newlines
        default:
            sendError(F("unknown command"));
            break;
    }
}
