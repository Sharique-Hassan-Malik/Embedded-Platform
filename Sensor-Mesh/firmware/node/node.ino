// ── NODE CONFIGURATION ────────────────────────────────────────────────────────
// Set NODE_ID to a unique value (0–7) for each physical node.
// Flash each Arduino with its own NODE_ID before deploying.
//
//   NODE_ID 0  →  base node (USB serial → PC, no local sensors)
//   NODE_ID 1…7 → sensor node (DHT22 + LDR + PIR → mesh → base)
//
// In the Arduino IDE: Sketch → click node.ino tab → change the #define below.
// With arduino-cli: --build-property build.extra_flags="-DNODE_ID=2"

#ifndef NODE_ID
#define NODE_ID 1
#endif

#include "config.h"
#include "packet.h"
#include "radio.h"
#include "router.h"

#if NODE_ID != 0
#include "sensor.h"
#endif

// ── Base node: serial output ──────────────────────────────────────────────────
// Format: "DATA,<origin>,<hops>,<temp_x10>,<hum_x10>,<light>,<motion>\n"
// The Python host parses this line format.
#if NODE_ID == 0
static void onDataReceived(uint8_t origin, uint8_t hops, const DataPayload &d) {
    Serial.print(F("DATA,"));
    Serial.print(origin);
    Serial.print(',');
    Serial.print(hops);
    Serial.print(',');
    Serial.print(d.temperature_x10);
    Serial.print(',');
    Serial.print(d.humidity_x10);
    Serial.print(',');
    Serial.print(d.light_adc);
    Serial.print(',');
    Serial.println(d.motion);
}
#endif

// ── Sensor node: periodic transmit ───────────────────────────────────────────
#if NODE_ID != 0
static uint32_t g_last_send = 0;
#endif

void setup() {
#if NODE_ID == 0
    Serial.begin(SERIAL_BAUD);
    Serial.println(F("READY"));
#endif

    if (!Radio::begin(NODE_ID)) {
#if NODE_ID == 0
        Serial.println(F("ERR,radio init failed"));
#endif
        while (true) {}  // halt — check wiring
    }

    Router::begin(NODE_ID);

#if NODE_ID == 0
    Router::onData(onDataReceived);
#endif

#if NODE_ID != 0
    Sensor::begin();
    // Stagger initial send to avoid all nodes transmitting simultaneously.
    g_last_send = millis() + static_cast<uint32_t>(NODE_ID) * 800UL;
#endif
}

void loop() {
    Router::update();

#if NODE_ID != 0
    uint32_t now = millis();
    if (now - g_last_send >= SENSOR_PERIOD_MS) {
        g_last_send = now;
        DataPayload data;
        Sensor::read(data);
        Router::sendData(BASE_NODE_ID, data);
    }
#endif
}
