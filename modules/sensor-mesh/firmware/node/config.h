#pragma once
#include <Arduino.h>

// ── Node identity ─────────────────────────────────────────────────────────────
// Set this to a unique value for each node before compiling and flashing.
// NODE_ID = 0  → base node (connected to PC via USB serial, no local sensors)
// NODE_ID = 1…7 → sensor node (reads sensors, sends data toward base)
#ifndef NODE_ID
#define NODE_ID 1
#endif

static constexpr uint8_t BASE_NODE_ID    = 0;
static constexpr uint8_t BROADCAST_ID   = 0xFF;  // destination meaning "all nodes"
static constexpr uint8_t MAX_NODES      = 8;     // maximum mesh size

// ── nRF24L01 wiring ───────────────────────────────────────────────────────────
// CE and CSN are the only configurable pins; MOSI/MISO/SCK are hardware SPI.
// On Arduino Uno/Nano: MOSI=D11, MISO=D12, SCK=D13.
static constexpr uint8_t RF_CE_PIN  = 9;
static constexpr uint8_t RF_CSN_PIN = 10;

// Radio channel (0–125). All nodes in the mesh must use the same channel.
static constexpr uint8_t RF_CHANNEL = 76;

// PA level: RF24_PA_LOW for bench testing; RF24_PA_HIGH for outdoor range.
// (value is passed directly to radio.setPALevel())
#define RF_PA_LEVEL RF24_PA_LOW

// Air data rate: RF24_250KBPS gives maximum range at the cost of throughput.
#define RF_DATA_RATE RF24_250KBPS

// ── Sensors (sensor nodes only — ignored by base) ─────────────────────────────
static constexpr uint8_t DHT_PIN    = 4;    // DHT22 data line
static constexpr uint8_t LDR_PIN    = A0;   // LDR voltage divider output
static constexpr uint8_t PIR_PIN    = 3;    // HC-SR501 PIR digital output

// How often a sensor node sends a data packet to the base (ms).
static constexpr uint32_t SENSOR_PERIOD_MS = 5000;

// ── Routing parameters ────────────────────────────────────────────────────────
static constexpr uint8_t  ROUTE_TABLE_SIZE  = 8;    // max routing table entries
static constexpr uint8_t  ROUTE_TTL_INIT    = 5;    // hop limit for RREQ/RREP
static constexpr uint8_t  MAX_RETRIES       = 3;    // per-hop send retries
static constexpr uint16_t ACK_TIMEOUT_MS    = 80;   // ms to wait for per-hop ACK
static constexpr uint32_t ROUTE_EXPIRE_MS   = 60000; // invalidate routes after 60 s
static constexpr uint8_t  DEDUP_TABLE_SIZE  = 16;   // (origin, seq) dedup cache

// ── Serial (base node → PC) ───────────────────────────────────────────────────
static constexpr uint32_t SERIAL_BAUD = 115200;
