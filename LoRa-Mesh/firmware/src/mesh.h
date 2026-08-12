/*
 * mesh.h — LoRa mesh protocol engine.
 *
 * The mesh engine sits between the radio driver and the application.
 * It handles:
 *   • Periodic HELLO broadcast (route advertisement)
 *   • Relay of HELLO and DATA packets (multi-hop forwarding)
 *   • Duplicate suppression
 *   • Routing table management
 *   • NACK generation when no route exists
 *   • Delivery of received DATA packets to the application callback
 *
 * The engine is driven from the Arduino loop() by calling mesh_update()
 * every iteration.  No interrupts are used; DIO0 is polled.
 */

#ifndef MESH_H
#define MESH_H

#include <stdint.h>
#include <stdbool.h>
#include "packet.h"
#include "routing.h"
#include "dup_table.h"

/* Called when a DATA packet addressed to this node arrives.
 *   payload, len : application data
 *   src          : originating node address
 *   rssi, snr    : of the last hop (not end-to-end) */
typedef void (*MeshRecvCallback)(const uint8_t *payload, uint8_t len,
                                  uint8_t src, int16_t rssi, int8_t snr);

/* Called when a NACK is received for a packet this node sent. */
typedef void (*MeshNackCallback)(uint8_t dest, uint8_t seq);

typedef struct {
    uint8_t          addr;          /* this node's address              */
    RoutingTable     routes;
    DupTable         dup;
    uint8_t          seq;           /* outgoing sequence counter        */
    uint32_t         last_hello_ms;
    MeshRecvCallback on_recv;
    MeshNackCallback on_nack;       /* may be NULL                      */

    /* Statistics */
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t relay_count;
    uint32_t drop_dup;
    uint32_t drop_ttl;
    uint32_t drop_no_route;
} MeshNode;

/* mesh_init — call once before mesh_update. */
void mesh_init(MeshNode *node, uint8_t addr,
               MeshRecvCallback on_recv, MeshNackCallback on_nack);

/*
 * mesh_send — queue a DATA packet to dst with payload.
 * Returns false if no route to dst is known.
 */
bool mesh_send(MeshNode *node, uint8_t dst,
               const uint8_t *payload, uint8_t len);

/*
 * mesh_update — poll the radio and process any received packet.
 * Also sends the periodic HELLO beacon.
 * Call from loop() as frequently as possible.
 */
void mesh_update(MeshNode *node);

/* mesh_route_count — number of valid entries in the routing table. */
uint8_t mesh_route_count(const MeshNode *node);

/* mesh_dump_routes — write routing table to Serial as human-readable text. */
void mesh_dump_routes(const MeshNode *node);

#endif /* MESH_H */
