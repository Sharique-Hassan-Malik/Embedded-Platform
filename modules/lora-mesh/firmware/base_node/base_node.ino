/*
 * base_node.ino — LoRa mesh base node (address 0x01).
 *
 * Receives DATA packets from any node in the mesh, decodes the payload
 * and emits structured JSON lines to the host PC over USB serial at
 * 115200 baud.  The Python dashboard (host/dashboard.py) parses these.
 *
 * JSON frame format:
 *   {"type":"data","src":2,"rssi":-78,"snr":7,"hops":2,
 *    "temp_c":23.45,"hum_pct":61.20,"uptime_s":3600}
 *
 * Status frames (emitted every STATUS_INTERVAL_MS):
 *   {"type":"status","addr":1,"routes":3,"tx":12,"rx":47,
 *    "relay":8,"drop_dup":2,"drop_ttl":0,"drop_no_route":0}
 *
 * The base node also acts as a full mesh relay: it rebroadcasts HELLO
 * packets and forwards DATA packets between nodes that cannot hear each
 * other directly.
 */

#include <Arduino.h>
#include "radio.h"
#include "mesh.h"

#define STATUS_INTERVAL_MS 10000u

static MeshNode g_node;
static uint32_t g_last_status_ms = 0;

/* ── Payload decoder ──────────────────────────────────────────────────────── */
/*
 *  Byte 0:     node address (redundant, but useful for cross-check)
 *  Bytes 1–4:  temperature × 100 as int32 LE
 *  Bytes 5–8:  humidity × 100 as int32 LE
 *  Bytes 9–12: uptime seconds as uint32 LE
 */
static void _decode_and_print(const uint8_t *payload, uint8_t len,
                               uint8_t src, int16_t rssi, int8_t snr,
                               uint8_t hops)
{
    if (len < 13) {
        Serial.print(F("{\"type\":\"raw\",\"src\":"));
        Serial.print(src);
        Serial.print(F(",\"len\":"));
        Serial.print(len);
        Serial.println(F("}"));
        return;
    }

    int32_t  temp_raw;
    int32_t  hum_raw;
    uint32_t uptime;
    memcpy(&temp_raw, &payload[1], 4);
    memcpy(&hum_raw,  &payload[5], 4);
    memcpy(&uptime,   &payload[9], 4);

    float temp_c  = temp_raw  / 100.0f;
    float hum_pct = hum_raw   / 100.0f;

    Serial.print(F("{\"type\":\"data\""));
    Serial.print(F(",\"src\":")); Serial.print(src);
    Serial.print(F(",\"rssi\":")); Serial.print(rssi);
    Serial.print(F(",\"snr\":")); Serial.print((int)(snr / 4));
    Serial.print(F(",\"hops\":")); Serial.print(hops);
    Serial.print(F(",\"temp_c\":"));
    Serial.print(temp_c, 2);
    Serial.print(F(",\"hum_pct\":"));
    Serial.print(hum_pct, 2);
    Serial.print(F(",\"uptime_s\":")); Serial.print(uptime);
    Serial.println(F("}"));
}

/* ── Mesh callbacks ──────────────────────────────────────────────────────── */

static uint8_t  _last_src_hops = 0;
static int16_t  _last_src_rssi = 0;
static int8_t   _last_src_snr  = 0;

static void on_recv(const uint8_t *payload, uint8_t len,
                    uint8_t src, int16_t rssi, int8_t snr)
{
    /* hops stored in the last received packet — accessible via global stash */
    _decode_and_print(payload, len, src, rssi, snr, _last_src_hops);
}

static void on_nack(uint8_t dest, uint8_t seq)
{
    Serial.print(F("{\"type\":\"nack\",\"dest\":"));
    Serial.print(dest);
    Serial.print(F(",\"seq\":"));
    Serial.print(seq);
    Serial.println(F("}"));
}

/* ── Status frame ────────────────────────────────────────────────────────── */

static void _print_status(void)
{
    Serial.print(F("{\"type\":\"status\""));
    Serial.print(F(",\"addr\":")); Serial.print(g_node.addr);
    Serial.print(F(",\"routes\":")); Serial.print(mesh_route_count(&g_node));
    Serial.print(F(",\"tx\":")); Serial.print(g_node.tx_count);
    Serial.print(F(",\"rx\":")); Serial.print(g_node.rx_count);
    Serial.print(F(",\"relay\":")); Serial.print(g_node.relay_count);
    Serial.print(F(",\"drop_dup\":")); Serial.print(g_node.drop_dup);
    Serial.print(F(",\"drop_ttl\":")); Serial.print(g_node.drop_ttl);
    Serial.print(F(",\"drop_no_route\":")); Serial.print(g_node.drop_no_route);
    Serial.println(F("}"));
}

/* ── setup / loop ────────────────────────────────────────────────────────── */

void setup()
{
    Serial.begin(115200);
    Serial.println(F("{\"type\":\"boot\",\"addr\":1}"));

    if (radio_init() != RADIO_OK) {
        Serial.println(F("{\"type\":\"error\",\"msg\":\"radio_init\"}"));
        while (true) delay(1000);
    }

    mesh_init(&g_node, ADDR_BASE, on_recv, on_nack);
    Serial.println(F("{\"type\":\"ready\"}"));
}

void loop()
{
    mesh_update(&g_node);

    if (millis() - g_last_status_ms >= STATUS_INTERVAL_MS) {
        g_last_status_ms = millis();
        _print_status();
    }
}
