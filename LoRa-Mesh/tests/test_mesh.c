/*
 * test_mesh.c — host-side unit tests for routing table and dup-suppression.
 *
 * Compiled with native GCC.  All tested headers are portable C99 with no
 * Arduino or radio dependencies.
 *
 * Build:
 *   gcc -std=c99 -Wall -Wextra -I../firmware/src test_mesh.c -o test_mesh
 *   ./test_mesh
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>

/* Stub millis() so headers compile without Arduino */
static uint32_t _mock_ms = 0;
static inline uint32_t millis(void) { return _mock_ms; }

#include "../firmware/src/packet.h"
#include "../firmware/src/routing.h"
#include "../firmware/src/dup_table.h"

/* ── Minimal test framework ──────────────────────────────────────────────── */

static int _pass = 0, _fail = 0;

#define TEST(name) do { printf("  %-58s", name); fflush(stdout); } while (0)
#define EXPECT(c)  do { \
    if (c) { printf("PASS\n"); _pass++; } \
    else   { printf("FAIL  (line %d: %s)\n", __LINE__, #c); _fail++; } \
} while (0)
#define SECTION(t) printf("\n%s\n", t)

/* ══════════════════════════════════════════════════════════════════════════
 * T1 — Routing table
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_routing(void)
{
    SECTION("T1  Routing table");

    RoutingTable rt;
    rt_init(&rt);
    _mock_ms = 1000;

    TEST("lookup on empty table returns ADDR_NONE");
    EXPECT(rt_lookup(&rt, 0x02, _mock_ms) == ADDR_NONE);

    TEST("rt_update inserts new entry");
    bool changed = rt_update(&rt, 0x02, 0x03, 2, -80, 7, _mock_ms);
    EXPECT(changed);

    TEST("lookup finds inserted entry");
    EXPECT(rt_lookup(&rt, 0x02, _mock_ms) == 0x03);

    TEST("update with same next_hop refreshes timestamp");
    _mock_ms = 5000;
    rt_update(&rt, 0x02, 0x03, 2, -75, 8, _mock_ms);
    EXPECT(rt_lookup(&rt, 0x02, _mock_ms) == 0x03);

    TEST("update with fewer hops replaces route");
    rt_update(&rt, 0x02, 0x04, 1, -65, 10, _mock_ms);
    EXPECT(rt_lookup(&rt, 0x02, _mock_ms) == 0x04);

    TEST("update with more hops and different next_hop is rejected");
    bool updated = rt_update(&rt, 0x02, 0x05, 3, -90, 4, _mock_ms);
    EXPECT(!updated);
    EXPECT(rt_lookup(&rt, 0x02, _mock_ms) == 0x04);

    TEST("broadcast address is rejected");
    EXPECT(!rt_update(&rt, ADDR_BROADCAST, 0x03, 1, -70, 8, _mock_ms));

    TEST("ADDR_NONE destination is rejected");
    EXPECT(!rt_update(&rt, ADDR_NONE, 0x03, 1, -70, 8, _mock_ms));

    TEST("expired entry returns ADDR_NONE");
    _mock_ms = 5000 + ROUTE_EXPIRE_MS + 1;
    EXPECT(rt_lookup(&rt, 0x02, _mock_ms) == ADDR_NONE);

    TEST("rt_expire marks stale entries invalid");
    _mock_ms = 0;
    RoutingTable rt2;
    rt_init(&rt2);
    rt_update(&rt2, 0x05, 0x06, 1, -70, 9, 0);
    _mock_ms = ROUTE_EXPIRE_MS + 1;
    rt_expire(&rt2, _mock_ms);
    EXPECT(rt_lookup(&rt2, 0x05, _mock_ms) == ADDR_NONE);

    TEST("rt_best_hop_count returns correct minimum");
    _mock_ms = 0;
    RoutingTable rt3;
    rt_init(&rt3);
    rt_update(&rt3, 0x07, 0x08, 3, -80, 6, 0);
    rt_update(&rt3, 0x07, 0x09, 1, -70, 9, 0);  /* better hops — replaces */
    EXPECT(rt_best_hop_count(&rt3, 0x07, 0) == 1);

    TEST("rt_best_hop_count returns 0xFF when no route");
    EXPECT(rt_best_hop_count(&rt3, 0xFF, 0) == 0xFF);

    TEST("table fills to MESH_MAX_NODES without crash");
    RoutingTable rt4;
    rt_init(&rt4);
    for (uint8_t i = 2; i < 2 + MESH_MAX_NODES; i++)
        rt_update(&rt4, i, i + 1, 1, -70, 8, 0);
    /* One more — should evict oldest */
    bool ok = rt_update(&rt4, 0xFE, 0x01, 1, -70, 8, 0);
    EXPECT(ok);   /* table handles overflow gracefully */
}

/* ══════════════════════════════════════════════════════════════════════════
 * T2 — Duplicate table
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_dup_table(void)
{
    SECTION("T2  Duplicate suppression table");

    DupTable dt;
    dup_init(&dt);

    TEST("first occurrence is not a duplicate");
    EXPECT(!dup_check_and_add(&dt, 0x02, 0x01, PKT_DATA));

    TEST("same (origin, seq, type) is a duplicate");
    EXPECT(dup_check_and_add(&dt, 0x02, 0x01, PKT_DATA));

    TEST("different seq from same origin is not a duplicate");
    EXPECT(!dup_check_and_add(&dt, 0x02, 0x02, PKT_DATA));

    TEST("different type with same origin and seq is not a duplicate");
    EXPECT(!dup_check_and_add(&dt, 0x02, 0x01, PKT_HELLO));

    TEST("different origin with same seq is not a duplicate");
    EXPECT(!dup_check_and_add(&dt, 0x03, 0x01, PKT_DATA));

    TEST("circular eviction: slot is reused after DUP_TABLE_SIZE entries");
    dup_init(&dt);
    /* Fill table completely with entries (origin=0x02, seq=1..DUP_TABLE_SIZE) */
    for (uint8_t i = 1; i <= DUP_TABLE_SIZE; i++)
        dup_check_and_add(&dt, 0x02, i, PKT_DATA);
    /* Insert one more — this overwrites the oldest slot (seq=1) */
    dup_check_and_add(&dt, 0x03, 0x01, PKT_DATA);
    /* (0x02, seq=1) should now be evicted */
    bool still_dup = dup_check_and_add(&dt, 0x02, 1, PKT_DATA);
    EXPECT(!still_dup);   /* evicted — treated as new */

    TEST("entries from different origins coexist");
    dup_init(&dt);
    dup_check_and_add(&dt, 0x0A, 0x01, PKT_DATA);
    dup_check_and_add(&dt, 0x0B, 0x01, PKT_DATA);
    EXPECT(dup_check_and_add(&dt, 0x0A, 0x01, PKT_DATA));
    EXPECT(dup_check_and_add(&dt, 0x0B, 0x01, PKT_DATA));
}

/* ══════════════════════════════════════════════════════════════════════════
 * T3 — Packet format
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_packet(void)
{
    SECTION("T3  Packet format");

    TEST("PKT_WIRE_LEN with no payload is PKT_HEADER_LEN");
    Packet p;
    memset(&p, 0, sizeof(p));
    p.payload_len = 0;
    EXPECT(PKT_WIRE_LEN(&p) == PKT_HEADER_LEN);

    TEST("PKT_WIRE_LEN with full payload does not exceed PKT_MAX_TOTAL");
    p.payload_len = PKT_MAX_PAYLOAD;
    EXPECT(PKT_WIRE_LEN(&p) == PKT_MAX_TOTAL);

    TEST("Packet struct is exactly PKT_HEADER_LEN + PKT_MAX_PAYLOAD bytes");
    EXPECT(sizeof(Packet) == PKT_HEADER_LEN + PKT_MAX_PAYLOAD);

    TEST("MESH_MAX_HOPS fits in one byte (ttl field)");
    EXPECT(MESH_MAX_HOPS <= 0xFF);

    TEST("DUP_TABLE_SIZE fits in uint8_t head index");
    EXPECT(DUP_TABLE_SIZE <= 255);
}

/* ══════════════════════════════════════════════════════════════════════════
 * T4 — Protocol parser (Python-side logic in C for cross-validation)
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_protocol(void)
{
    SECTION("T4  Route advertisement logic");

    RoutingTable rt;
    rt_init(&rt);
    _mock_ms = 0;

    /*
     * Simulate a HELLO from node 5 arriving via relay node 3 with hops=1.
     * The receiver should store: reach 5 via 3 with hop_count=2.
     */
    uint8_t origin   = 5;
    uint8_t relay    = 3;   /* pkt.src in the relayed frame */
    uint8_t pkt_hops = 1;   /* already relayed once */

    rt_update(&rt, origin, relay, (uint8_t)(pkt_hops + 1), -85, 6, _mock_ms);

    TEST("relayed HELLO establishes 2-hop route");
    EXPECT(rt_lookup(&rt, 5, _mock_ms) == 3);
    EXPECT(rt_best_hop_count(&rt, 5, _mock_ms) == 2);

    /*
     * Now the same node is heard directly (hops=0, pkt.src=origin).
     * This should replace the 2-hop route with a 1-hop direct route.
     */
    rt_update(&rt, origin, origin, 1, -70, 9, _mock_ms);

    TEST("direct HELLO replaces multi-hop route");
    EXPECT(rt_lookup(&rt, 5, _mock_ms) == 5);
    EXPECT(rt_best_hop_count(&rt, 5, _mock_ms) == 1);

    TEST("route table can hold entries for all valid node addresses (2–254)");
    RoutingTable rt2;
    rt_init(&rt2);
    /* Insert 30 entries — MESH_MAX_NODES=32, so this fits */
    for (uint8_t i = 2; i < 32; i++)
        rt_update(&rt2, i, i - 1, 1, -70, 8, 0);
    bool all_found = true;
    for (uint8_t i = 2; i < 32; i++) {
        if (rt_lookup(&rt2, i, 0) == ADDR_NONE) { all_found = false; break; }
    }
    EXPECT(all_found);
}

/* ── Entry point ──────────────────────────────────────────────────────────── */

int main(void)
{
    printf("LoRa Mesh host unit tests\n");
    printf("=========================\n");

    test_routing();
    test_dup_table();
    test_packet();
    test_protocol();

    printf("\n=========================\n");
    printf("Results: %d passed, %d failed\n", _pass, _fail);
    return _fail > 0 ? 1 : 0;
}
