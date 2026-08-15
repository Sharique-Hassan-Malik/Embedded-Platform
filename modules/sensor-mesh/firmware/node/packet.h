#pragma once
#include <Arduino.h>

// ── Packet types ──────────────────────────────────────────────────────────────
// Encoded in the low nibble of the flags byte.
static constexpr uint8_t PKT_DATA  = 0x01;  // sensor data: node → base
static constexpr uint8_t PKT_RREQ  = 0x02;  // route request: broadcast
static constexpr uint8_t PKT_RREP  = 0x03;  // route reply: unicast back
static constexpr uint8_t PKT_ACK   = 0x04;  // per-hop acknowledgement
static constexpr uint8_t PKT_NACK  = 0x05;  // negative ACK (no route)

// ── Wire packet structure (exactly 32 bytes — nRF24L01 max payload) ───────────
//
// Byte  0      dest       destination node ID (or BROADCAST_ID)
// Byte  1      src        immediate sender (previous hop)
// Byte  2      origin     packet creator (never changes during forwarding)
// Byte  3      seq        per-origin sequence number (wraps 0–255)
// Byte  4      flags      [7:4] = TTL remaining, [3:0] = packet type
// Byte  5      hop_count  incremented at each hop
// Bytes 6–31   payload    26-byte type-specific payload
//
// The nRF24L01 hardware auto-ACK is disabled; the routing layer implements its
// own per-hop ACK (PKT_ACK) so that retransmissions are handled per hop rather
// than end-to-end. This avoids the nRF24L01's static addressing constraint.

struct __attribute__((packed)) Packet {
    uint8_t dest;
    uint8_t src;
    uint8_t origin;
    uint8_t seq;
    uint8_t flags;          // [7:4] TTL, [3:0] type
    uint8_t hop_count;
    uint8_t payload[26];

    uint8_t type() const { return flags & 0x0F; }
    uint8_t ttl()  const { return (flags >> 4) & 0x0F; }

    void setType(uint8_t t) { flags = (flags & 0xF0) | (t & 0x0F); }
    void setTTL(uint8_t v)  { flags = (flags & 0x0F) | ((v & 0x0F) << 4); }
};

static_assert(sizeof(Packet) == 32, "Packet must be exactly 32 bytes");

// ── Payload layouts ───────────────────────────────────────────────────────────

// PKT_DATA payload (bytes 0–7 of payload[]):
//   int16  temperature_x10   (e.g. 234 = 23.4 °C; -999 = read error)
//   uint16 humidity_x10      (e.g. 652 = 65.2 %;  0xFFFF = read error)
//   uint16 light_adc         (0–1023 raw ADC)
//   uint8  motion            (0 = no motion, 1 = motion detected)
//   uint8  battery_pct       (0–100; 0xFF = not measured)
struct __attribute__((packed)) DataPayload {
    int16_t  temperature_x10;
    uint16_t humidity_x10;
    uint16_t light_adc;
    uint8_t  motion;
    uint8_t  battery_pct;
};

// PKT_RREQ payload (bytes 0–1 of payload[]):
//   uint8  rreq_dest    — node ID being sought
//   uint8  rreq_seq     — destination's last known sequence number (for freshness)
struct __attribute__((packed)) RREQPayload {
    uint8_t rreq_dest;
    uint8_t rreq_seq;
};

// PKT_RREP payload (bytes 0–2 of payload[]):
//   uint8  rrep_dest       — destination that was found
//   uint8  metric          — hop count to that destination from replier
//   uint8  dest_seq        — destination's current sequence number
struct __attribute__((packed)) RREPPayload {
    uint8_t rrep_dest;
    uint8_t metric;
    uint8_t dest_seq;
};

// PKT_ACK payload (bytes 0–1 of payload[]):
//   uint8  ack_for_seq   — seq number being acknowledged
//   uint8  ack_origin    — origin of the packet being acknowledged
struct __attribute__((packed)) AckPayload {
    uint8_t ack_for_seq;
    uint8_t ack_origin;
};
