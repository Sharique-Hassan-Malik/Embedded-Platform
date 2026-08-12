/*
 * node.ino — LoRa mesh sensor node.
 *
 * Reads temperature and humidity from a DHT22 sensor (pin D4) and sends
 * a DATA packet to the base node (address 0x01) every SEND_INTERVAL_MS.
 * Multi-hop relay is handled automatically by mesh_update().
 *
 * Set NODE_ADDR to a unique value (0x02–0xFE) for each node in the mesh.
 * The base node runs base_node.ino with address 0x01.
 *
 * Serial output (115200 baud):
 *   BOOT addr=0x03
 *   HELLO sent seq=0
 *   ROUTE dest=0x01 via=0x02 hops=2 rssi=-78
 *   DATA sent seq=3 dst=0x01 len=8
 *   RELAY PKT_HELLO origin=0x02 hops=1
 *   RECV DATA from=0x01 rssi=-65
 */

#include <Arduino.h>
#include "radio.h"
#include "mesh.h"

/* ── Configuration (edit per node) ───────────────────────────────────────── */
#define NODE_ADDR         0x02u   /* unique per node; base node is 0x01   */
#define SEND_INTERVAL_MS  30000u  /* data transmit interval                */

/* DHT22 driver — simple bit-banged read, no library */
#define DHT_PIN           4

/* ── State ───────────────────────────────────────────────────────────────── */
static MeshNode g_node;
static uint32_t g_last_send_ms = 0;

/* ── DHT22 bit-banged reader ──────────────────────────────────────────────── */

static bool dht_read(float *temp_c, float *hum_pct)
{
    uint8_t data[5] = {0};

    /* Start signal */
    pinMode(DHT_PIN, OUTPUT);
    digitalWrite(DHT_PIN, LOW);
    delay(18);
    digitalWrite(DHT_PIN, HIGH);
    delayMicroseconds(30);
    pinMode(DHT_PIN, INPUT_PULLUP);

    /* Wait for sensor response */
    uint32_t t = micros();
    while (digitalRead(DHT_PIN) == HIGH) if (micros() - t > 200) return false;
    t = micros();
    while (digitalRead(DHT_PIN) == LOW)  if (micros() - t > 200) return false;
    t = micros();
    while (digitalRead(DHT_PIN) == HIGH) if (micros() - t > 200) return false;

    /* Read 40 bits */
    for (uint8_t i = 0; i < 40; i++) {
        t = micros();
        while (digitalRead(DHT_PIN) == LOW)  if (micros() - t > 100) return false;
        delayMicroseconds(35);
        data[i / 8] <<= 1;
        if (digitalRead(DHT_PIN) == HIGH) data[i / 8] |= 1;
        t = micros();
        while (digitalRead(DHT_PIN) == HIGH) if (micros() - t > 100) return false;
    }

    /* Checksum */
    if (data[4] != ((data[0] + data[1] + data[2] + data[3]) & 0xFF))
        return false;

    *hum_pct = ((uint16_t)(data[0] << 8) | data[1]) * 0.1f;
    uint16_t raw_temp = ((uint16_t)((data[2] & 0x7F) << 8)) | data[3];
    *temp_c = raw_temp * 0.1f;
    if (data[2] & 0x80) *temp_c = -*temp_c;   /* negative temperature */
    return true;
}

/* ── Mesh callbacks ──────────────────────────────────────────────────────── */

static void on_recv(const uint8_t *payload, uint8_t len,
                    uint8_t src, int16_t rssi, int8_t snr)
{
    Serial.print(F("RECV DATA from=0x"));
    Serial.print(src, HEX);
    Serial.print(F(" rssi="));
    Serial.print(rssi);
    Serial.print(F(" len="));
    Serial.println(len);
}

static void on_nack(uint8_t dest, uint8_t seq)
{
    Serial.print(F("NACK dest=0x"));
    Serial.print(dest, HEX);
    Serial.print(F(" seq="));
    Serial.println(seq);
}

/* ── Payload format ──────────────────────────────────────────────────────── */
/*
 *  Byte 0:     node address
 *  Bytes 1–4:  temperature × 100 as int32 (little-endian)
 *  Bytes 5–8:  humidity × 100 as int32 (little-endian)
 *  Bytes 9–12: millis() / 1000 as uint32 (uptime seconds)
 */
static uint8_t _build_payload(uint8_t *buf, float temp_c, float hum_pct)
{
    int32_t temp_int = (int32_t)(temp_c * 100.0f);
    int32_t hum_int  = (int32_t)(hum_pct * 100.0f);
    uint32_t uptime  = millis() / 1000UL;

    buf[0] = NODE_ADDR;
    memcpy(&buf[1], &temp_int, 4);
    memcpy(&buf[5], &hum_int,  4);
    memcpy(&buf[9], &uptime,   4);
    return 13;
}

/* ── setup / loop ────────────────────────────────────────────────────────── */

void setup()
{
    Serial.begin(115200);
    Serial.print(F("BOOT addr=0x"));
    Serial.println(NODE_ADDR, HEX);

    if (radio_init() != RADIO_OK) {
        Serial.println(F("ERROR radio_init failed"));
        while (true) delay(1000);
    }

    mesh_init(&g_node, NODE_ADDR, on_recv, on_nack);
    Serial.println(F("Mesh ready"));
}

void loop()
{
    mesh_update(&g_node);

    if (millis() - g_last_send_ms >= SEND_INTERVAL_MS) {
        g_last_send_ms = millis();

        float temp_c = 0.0f, hum_pct = 0.0f;
        bool ok = dht_read(&temp_c, &hum_pct);

        if (!ok) {
            Serial.println(F("WARN DHT read failed"));
            return;
        }

        uint8_t payload[16];
        uint8_t len = _build_payload(payload, temp_c, hum_pct);

        if (mesh_send(&g_node, ADDR_BASE, payload, len)) {
            Serial.print(F("DATA sent temp="));
            Serial.print(temp_c, 1);
            Serial.print(F("C hum="));
            Serial.print(hum_pct, 1);
            Serial.println(F("%"));
        } else {
            Serial.println(F("WARN no route to base"));
        }

        /* Print routing table every 5 transmissions */
        static uint8_t send_count = 0;
        if ((++send_count % 5) == 0) mesh_dump_routes(&g_node);
    }
}
