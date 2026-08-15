/*
 * routing.h — distance-vector routing table.
 *
 * Each entry maps a destination address to the best known next hop.
 * Entries are learned from HELLO packets: a node that hears a HELLO
 * from origin X (via relay Y) records "reach X through Y with N hops".
 * Entries expire after ROUTE_EXPIRE_MS milliseconds of silence.
 *
 * The table is not dynamically allocated — all storage is static.
 * MESH_MAX_NODES is the compile-time maximum number of destinations.
 */

#ifndef ROUTING_H
#define ROUTING_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>   // NULL
#include "packet.h"

typedef struct {
    uint8_t  dest;           /* destination node address                  */
    uint8_t  next_hop;       /* address of the immediate relay node       */
    uint8_t  hop_count;      /* total hops to dest (1 = direct neighbour) */
    int16_t  rssi;           /* RSSI of the HELLO that established route  */
    int8_t   snr;            /* SNR of the HELLO that established route   */
    uint32_t last_seen_ms;   /* millis() / host time when last updated    */
    bool     valid;
} RouteEntry;

typedef struct {
    RouteEntry entries[MESH_MAX_NODES];
    uint8_t    count;
} RoutingTable;

/* Initialise all entries as invalid. */
static inline void rt_init(RoutingTable *rt)
{
    for (uint8_t i = 0; i < MESH_MAX_NODES; i++) rt->entries[i].valid = false;
    rt->count = 0;
}

/*
 * rt_update — insert or update a route to dest.
 *
 * A new route is accepted when:
 *   • no valid route to dest exists, or
 *   • the new hop_count is lower than the stored one, or
 *   • the route comes from the same next_hop (refresh).
 *
 * Returns true if the table was modified.
 */
static inline bool rt_update(RoutingTable *rt,
                              uint8_t dest, uint8_t next_hop,
                              uint8_t hop_count, int16_t rssi, int8_t snr,
                              uint32_t now_ms)
{
    /* Never store a route to broadcast or the reserved address */
    if (dest == ADDR_BROADCAST || dest == ADDR_NONE) return false;

    RouteEntry *slot   = NULL;
    RouteEntry *oldest = NULL;

    for (uint8_t i = 0; i < MESH_MAX_NODES; i++) {
        RouteEntry *e = &rt->entries[i];
        if (e->valid && e->dest == dest) {
            /* Existing entry: accept if same next_hop or fewer hops */
            if (e->next_hop != next_hop && hop_count >= e->hop_count) return false;
            e->next_hop     = next_hop;
            e->hop_count    = hop_count;
            e->rssi         = rssi;
            e->snr          = snr;
            e->last_seen_ms = now_ms;
            return true;
        }
        if (!e->valid && slot == NULL) slot = e;
        if (e->valid) {
            if (oldest == NULL || e->last_seen_ms < oldest->last_seen_ms)
                oldest = e;
        }
    }

    /* Use a free slot; evict the oldest valid entry if full */
    if (slot == NULL) slot = oldest;
    if (slot == NULL) return false;

    slot->dest         = dest;
    slot->next_hop     = next_hop;
    slot->hop_count    = hop_count;
    slot->rssi         = rssi;
    slot->snr          = snr;
    slot->last_seen_ms = now_ms;
    slot->valid        = true;
    return true;
}

/*
 * rt_lookup — find the next hop for dest.
 * Returns ADDR_NONE if no valid route exists or the route has expired.
 */
static inline uint8_t rt_lookup(const RoutingTable *rt, uint8_t dest,
                                 uint32_t now_ms)
{
    for (uint8_t i = 0; i < MESH_MAX_NODES; i++) {
        const RouteEntry *e = &rt->entries[i];
        if (!e->valid || e->dest != dest) continue;
        if (now_ms - e->last_seen_ms > ROUTE_EXPIRE_MS) continue;
        return e->next_hop;
    }
    return ADDR_NONE;
}

/*
 * rt_expire — mark entries older than ROUTE_EXPIRE_MS as invalid.
 * Call from the main loop or a periodic tick.
 */
static inline void rt_expire(RoutingTable *rt, uint32_t now_ms)
{
    for (uint8_t i = 0; i < MESH_MAX_NODES; i++) {
        RouteEntry *e = &rt->entries[i];
        if (e->valid && (now_ms - e->last_seen_ms > ROUTE_EXPIRE_MS))
            e->valid = false;
    }
}

/* rt_best_hop_count — lowest hop_count to dest, or 0xFF if not reachable */
static inline uint8_t rt_best_hop_count(const RoutingTable *rt, uint8_t dest,
                                         uint32_t now_ms)
{
    uint8_t best = 0xFF;
    for (uint8_t i = 0; i < MESH_MAX_NODES; i++) {
        const RouteEntry *e = &rt->entries[i];
        if (!e->valid || e->dest != dest) continue;
        if (now_ms - e->last_seen_ms > ROUTE_EXPIRE_MS) continue;
        if (e->hop_count < best) best = e->hop_count;
    }
    return best;
}

#endif /* ROUTING_H */
