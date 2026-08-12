#include "mesh.h"
#include "radio.h"

#include <Arduino.h>
#include <string.h>

/* ── Internal helpers ────────────────────────────────────────────────────── */

static uint8_t _tx_buf[PKT_MAX_TOTAL];

static void _send_packet(Packet *pkt)
{
    uint8_t wire_len = PKT_WIRE_LEN(pkt);
    memcpy(_tx_buf, pkt, wire_len);
    radio_send(_tx_buf, wire_len);
    radio_start_rx();
}

static void _send_hello(MeshNode *node)
{
    Packet pkt;
    pkt.type        = PKT_HELLO;
    pkt.src         = node->addr;
    pkt.dst         = ADDR_BROADCAST;
    pkt.origin      = node->addr;
    pkt.ttl         = MESH_MAX_HOPS;
    pkt.seq         = node->seq++;
    pkt.hops        = 0;
    pkt.payload_len = 0;
    _send_packet(&pkt);
    node->tx_count++;
}

static void _send_nack(MeshNode *node, uint8_t dest, uint8_t orig_seq)
{
    Packet pkt;
    pkt.type           = PKT_NACK;
    pkt.src            = node->addr;
    pkt.dst            = ADDR_BROADCAST;
    pkt.origin         = node->addr;
    pkt.ttl            = MESH_MAX_HOPS;
    pkt.seq            = node->seq++;
    pkt.hops           = 0;
    pkt.payload_len    = 2;
    pkt.payload[0]     = dest;     /* destination that could not be reached */
    pkt.payload[1]     = orig_seq; /* seq of the failed DATA packet         */
    _send_packet(&pkt);
}

/* ── mesh_init ───────────────────────────────────────────────────────────── */

void mesh_init(MeshNode *node, uint8_t addr,
               MeshRecvCallback on_recv, MeshNackCallback on_nack)
{
    memset(node, 0, sizeof(*node));
    node->addr    = addr;
    node->on_recv = on_recv;
    node->on_nack = on_nack;
    rt_init(&node->routes);
    dup_init(&node->dup);
    node->seq           = 0;
    node->last_hello_ms = 0;
    radio_start_rx();
}

/* ── mesh_send ───────────────────────────────────────────────────────────── */

bool mesh_send(MeshNode *node, uint8_t dst,
               const uint8_t *payload, uint8_t len)
{
    uint32_t now = millis();

    uint8_t next_hop = rt_lookup(&node->routes, dst, now);
    if (next_hop == ADDR_NONE) {
        node->drop_no_route++;
        return false;
    }

    Packet pkt;
    pkt.type        = PKT_DATA;
    pkt.src         = node->addr;
    pkt.dst         = next_hop;
    pkt.origin      = node->addr;
    pkt.ttl         = MESH_MAX_HOPS;
    pkt.seq         = node->seq++;
    pkt.hops        = 0;
    pkt.payload_len = (len <= PKT_MAX_PAYLOAD) ? len : PKT_MAX_PAYLOAD;
    memcpy(pkt.payload, payload, pkt.payload_len);

    dup_check_and_add(&node->dup, node->addr, pkt.seq, PKT_DATA);

    _send_packet(&pkt);
    node->tx_count++;
    return true;
}

/* ── _process_packet — called for every valid received packet ────────────── */

static void _process_packet(MeshNode *node, Packet *pkt,
                              int16_t rssi, int8_t snr)
{
    uint32_t now = millis();
    node->rx_count++;

    /* Duplicate suppression */
    if (dup_check_and_add(&node->dup, pkt->origin, pkt->seq, pkt->type)) {
        node->drop_dup++;
        return;
    }

    switch (pkt->type) {

    /* ── HELLO ─────────────────────────────────────────────────────────── */
    case PKT_HELLO: {
        /*
         * Learn route: origin is reachable via the immediate sender (src).
         * hop_count = pkt->hops + 1 because we are one hop further away.
         */
        uint8_t hops = pkt->hops + 1;
        rt_update(&node->routes, pkt->origin, pkt->src,
                   hops, rssi, snr, now);

        /* Rebroadcast if TTL allows and we haven't sent this HELLO before */
        if (pkt->ttl > 1) {
            Packet fwd = *pkt;
            fwd.src  = node->addr;
            fwd.ttl--;
            fwd.hops++;
            _send_packet(&fwd);
            node->relay_count++;
        } else {
            node->drop_ttl++;
        }
        break;
    }

    /* ── DATA ──────────────────────────────────────────────────────────── */
    case PKT_DATA: {
        if (pkt->dst == node->addr) {
            /* Deliver to application */
            if (node->on_recv)
                node->on_recv(pkt->payload, pkt->payload_len,
                               pkt->origin, rssi, snr);

            /* Send ACK back toward origin */
            uint8_t ack_hop = rt_lookup(&node->routes, pkt->origin, now);
            if (ack_hop != ADDR_NONE) {
                Packet ack;
                ack.type        = PKT_ACK;
                ack.src         = node->addr;
                ack.dst         = ack_hop;
                ack.origin      = node->addr;
                ack.ttl         = MESH_MAX_HOPS;
                ack.seq         = node->seq++;
                ack.hops        = 0;
                ack.payload_len = 2;
                ack.payload[0]  = pkt->origin; /* who to deliver ack to */
                ack.payload[1]  = pkt->seq;    /* ack'ing this seq       */
                _send_packet(&ack);
            }
        } else {
            /* Forward toward destination */
            if (pkt->ttl == 0) { node->drop_ttl++; break; }
            uint8_t next = rt_lookup(&node->routes, pkt->dst, now);
            if (next == ADDR_NONE) {
                node->drop_no_route++;
                _send_nack(node, pkt->dst, pkt->seq);
                break;
            }
            Packet fwd = *pkt;
            fwd.src = node->addr;
            fwd.dst = next;
            fwd.ttl--;
            fwd.hops++;
            _send_packet(&fwd);
            node->relay_count++;
        }
        break;
    }

    /* ── ACK ───────────────────────────────────────────────────────────── */
    case PKT_ACK: {
        if (pkt->payload_len < 2) break;
        uint8_t final_dest = pkt->payload[0];

        if (final_dest == node->addr) {
            /* ACK is for us — nothing more to do (no retransmit queue) */
        } else {
            /* Forward toward final_dest */
            if (pkt->ttl == 0) { node->drop_ttl++; break; }
            uint8_t next = rt_lookup(&node->routes, final_dest, now);
            if (next == ADDR_NONE) break;
            Packet fwd = *pkt;
            fwd.src = node->addr;
            fwd.dst = next;
            fwd.ttl--;
            fwd.hops++;
            _send_packet(&fwd);
            node->relay_count++;
        }
        break;
    }

    /* ── NACK ──────────────────────────────────────────────────────────── */
    case PKT_NACK: {
        if (pkt->payload_len < 2) break;
        uint8_t unreachable = pkt->payload[0];
        uint8_t failed_seq  = pkt->payload[1];
        if (node->on_nack) node->on_nack(unreachable, failed_seq);
        /* Rebroadcast NACK so originator hears it */
        if (pkt->ttl > 1) {
            Packet fwd = *pkt;
            fwd.src = node->addr;
            fwd.ttl--;
            fwd.hops++;
            _send_packet(&fwd);
        }
        break;
    }

    default: break;
    }
}

/* ── mesh_update ─────────────────────────────────────────────────────────── */

void mesh_update(MeshNode *node)
{
    uint32_t now = millis();

    /* Periodic HELLO */
    if (now - node->last_hello_ms >= HELLO_INTERVAL_MS) {
        _send_hello(node);
        node->last_hello_ms = now;
        rt_expire(&node->routes, now);
    }

    /* Poll radio */
    if (!radio_packet_available()) return;

    static uint8_t _rx_buf[PKT_MAX_TOTAL];
    RadioRxMeta meta;
    RadioResult r = radio_recv(_rx_buf, sizeof(_rx_buf), &meta);

    if (r == RADIO_CRC || r == RADIO_EMPTY) {
        radio_start_rx();
        return;
    }

    if (meta.len < PKT_HEADER_LEN) {
        radio_start_rx();
        return;
    }

    Packet *pkt = (Packet *)_rx_buf;

    /* Ignore own transmissions that loop back */
    if (pkt->origin == node->addr) {
        radio_start_rx();
        return;
    }

    _process_packet(node, pkt, meta.rssi, meta.snr);
    radio_start_rx();
}

/* ── mesh_route_count ────────────────────────────────────────────────────── */

uint8_t mesh_route_count(const MeshNode *node)
{
    uint8_t  count = 0;
    uint32_t now   = millis();
    for (uint8_t i = 0; i < MESH_MAX_NODES; i++) {
        const RouteEntry *e = &node->routes.entries[i];
        if (e->valid && (now - e->last_seen_ms <= ROUTE_EXPIRE_MS)) count++;
    }
    return count;
}

/* ── mesh_dump_routes ────────────────────────────────────────────────────── */

void mesh_dump_routes(const MeshNode *node)
{
    uint32_t now = millis();
    Serial.println(F("dest  nexthop  hops  rssi  snr  age_ms"));
    for (uint8_t i = 0; i < MESH_MAX_NODES; i++) {
        const RouteEntry *e = &node->routes.entries[i];
        if (!e->valid) continue;
        uint32_t age = now - e->last_seen_ms;
        if (age > ROUTE_EXPIRE_MS) continue;
        Serial.print(e->dest);     Serial.print('\t');
        Serial.print(e->next_hop); Serial.print('\t');
        Serial.print(e->hop_count); Serial.print('\t');
        Serial.print(e->rssi);     Serial.print('\t');
        Serial.print(e->snr / 4);  Serial.print('\t');
        Serial.println(age);
    }
}
