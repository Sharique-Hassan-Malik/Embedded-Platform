/*
 * packet.h — wire format for the LoRa mesh protocol.
 *
 * All multi-byte fields are little-endian.
 *
 * Frame layout (max 247 bytes, SX1276 LoRa payload limit):
 *
 *   Byte 0   type       PKT_DATA | PKT_HELLO | PKT_ACK | PKT_NACK
 *   Byte 1   src        source node address (1–253)
 *   Byte 2   dst        destination address (ADDR_BROADCAST = 0xFF)
 *   Byte 3   origin     address of the node that first injected the packet
 *   Byte 4   ttl        decremented at each hop; dropped when 0
 *   Byte 5   seq        per-source sequence counter (wraps at 255)
 *   Byte 6   hops       number of hops traversed so far (0 = direct)
 *   Byte 7   payload_len  bytes of payload that follow
 *   Bytes 8+ payload    application data (up to PKT_MAX_PAYLOAD bytes)
 *
 * Address space:
 *   0x00        reserved / unassigned
 *   0x01        base node (gateway to host PC)
 *   0x02–0xFE   sensor nodes
 *   0xFF        broadcast (HELLO and NACK only)
 *
 * Sequence numbers wrap without gap: nodes detect duplicates by comparing
 * the received seq against the last-seen seq for that (origin, type) pair.
 * A packet is a duplicate when its seq equals the stored value.
 */

#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>

/* Packet types */
#define PKT_DATA    0x01u   /* application payload, unicast              */
#define PKT_HELLO   0x02u   /* beacon / route advertisement, broadcast   */
#define PKT_ACK     0x03u   /* delivery confirmation, unicast back       */
#define PKT_NACK    0x04u   /* no route to destination, broadcast        */

/* Special addresses */
#define ADDR_BASE       0x01u
#define ADDR_BROADCAST  0xFFu
#define ADDR_NONE       0x00u

/* Protocol limits */
#define PKT_HEADER_LEN    8u
#define PKT_MAX_PAYLOAD   239u   /* 247 - PKT_HEADER_LEN */
#define PKT_MAX_TOTAL     247u

/* Routing knobs */
#define MESH_MAX_HOPS     7u     /* TTL initial value; limits flood depth  */
#define MESH_MAX_NODES    32u    /* routing table capacity                 */
#define HELLO_INTERVAL_MS 5000u  /* how often each node broadcasts HELLO   */
#define ROUTE_EXPIRE_MS   30000u /* route entry invalid after this silence */
#define DUP_TABLE_SIZE    64u    /* recent (src, seq) pairs kept           */

/* Packed frame struct — mapped directly onto the SX1276 FIFO */
#pragma pack(push, 1)
typedef struct {
    uint8_t type;
    uint8_t src;
    uint8_t dst;
    uint8_t origin;
    uint8_t ttl;
    uint8_t seq;
    uint8_t hops;
    uint8_t payload_len;
    uint8_t payload[PKT_MAX_PAYLOAD];
} Packet;
#pragma pack(pop)

/* Helper: total wire length of a packet */
#define PKT_WIRE_LEN(p)  ((uint8_t)(PKT_HEADER_LEN + (p)->payload_len))

#endif /* PACKET_H */
