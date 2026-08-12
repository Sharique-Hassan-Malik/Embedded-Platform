#pragma once
#include <Arduino.h>
#include "packet.h"

// Wraps the RF24 library to provide two operations:
//   send()      — unicast a packet to a specific node address
//   broadcast() — send to all nodes on the broadcast pipe
//   receive()   — non-blocking poll; fills *out if a packet is waiting
//
// nRF24L01 addressing:
//   Each node listens on two pipes:
//     Pipe 1 (unicast):  0xF0F0F0F0 || NODE_ID
//     Pipe 0 (bcast):   0xF0F0F0F0FF  (same for all nodes)
//
// Hardware ACK (auto-ack) is disabled. The routing layer implements its own
// per-hop ACK (PKT_ACK) which gives the routing protocol control over retries.

namespace Radio {

// Initialise RF24 and open listening pipes for this node.
// Returns false if the radio is not responding (wiring error).
bool begin(uint8_t node_id);

// Send a packet to a specific destination node (unicast).
// Does NOT wait for a routing-layer ACK — that is handled by the router.
// Returns true if the RF24 hardware accepted the packet for transmission.
bool send(uint8_t dest_node, const Packet &pkt);

// Broadcast a packet to all nodes (pipe 0, shared address).
bool broadcast(const Packet &pkt);

// Non-blocking receive. Returns true and fills *out if a packet is available.
bool receive(Packet *out);

} // namespace Radio
