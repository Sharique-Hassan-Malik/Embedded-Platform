#pragma once
#include <Arduino.h>
#include "packet.h"

// Mesh routing protocol implemented from scratch.
//
// Algorithm: simplified AODV (Ad-hoc On-demand Distance Vector) with
// per-hop acknowledgement and hop-count metric.
//
// Route discovery:
//   1. When a node wants to reach destination D with no cached route, it
//      broadcasts a RREQ (route request): dest=BROADCAST, rreq_dest=D.
//   2. Intermediate nodes rebroadcast RREQ with hop_count++ if it is fresher
//      than any cached route to D. They also record the reverse path (origin
//      → previous hop) for RREP routing.
//   3. When D (or a node with a fresh route to D) receives the RREQ, it sends
//      a unicast RREP back along the reverse path.
//   4. Each node receiving RREP updates its routing table and forwards RREP
//      toward the RREQ originator.
//
// Data forwarding:
//   1. Sender looks up next_hop for destination in routing table.
//   2. Unicasts packet to next_hop, waits up to ACK_TIMEOUT_MS for PKT_ACK.
//   3. On no ACK: retry up to MAX_RETRIES times, then invalidate route entry
//      and trigger a fresh RREQ.
//   4. Intermediate nodes that successfully forward a packet send PKT_ACK
//      back to the immediate sender.
//
// Deduplication:
//   A circular cache of (origin, seq) pairs prevents processing the same
//   RREQ broadcast multiple times.

namespace Router {

void begin(uint8_t my_id);

// Call every loop() iteration.
// Receives pending radio packets and drives the state machine.
void update();

// Queue a DATA packet to be sent to dest.
// Will trigger route discovery if no route is cached.
void sendData(uint8_t dest, const DataPayload &data);

// True if a route to dest is in the routing table and not expired.
bool hasRoute(uint8_t dest);

// Number of hops to dest (255 = no route).
uint8_t hopCount(uint8_t dest);

// Callback: called on the base node when a DATA packet arrives from the mesh.
// Register before calling begin().
using DataCallback = void (*)(uint8_t origin, uint8_t hops, const DataPayload &data);
void onData(DataCallback cb);

} // namespace Router
