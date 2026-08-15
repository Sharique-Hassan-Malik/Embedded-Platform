# Architecture

## System overview

```
┌─────────────────────────────────────────────────────────────────┐
│  Sensor node (Arduino + SX1276)                                 │
│  node.ino → mesh.cpp → radio.cpp → SPI → SX1276                │
└───────────────────────────────┬─────────────────────────────────┘
                                │ LoRa RF (915 / 868 / 433 MHz)
               ┌────────────────┼────────────────┐
               │                │                │
┌──────────────┴──┐    ┌────────┴──────┐   ┌────┴──────────────┐
│  Sensor node    │    │  Relay node   │   │  Sensor node      │
│  (no USB)       │    │  (no USB)     │   │  (no USB)         │
└─────────────────┘    └───────────────┘   └───────────────────┘
                                │
               ┌────────────────┘
               │ LoRa RF
┌──────────────┴──────────────────────────────────────────────────┐
│  Base node (Arduino + SX1276, USB to PC)                        │
│  base_node.ino → mesh.cpp → radio.cpp → SPI → SX1276           │
│                           ↓ USB serial JSON                     │
└──────────────────────────────────────────────────────────────── ┘
                           │
               ┌───────────┘
               │ USB / pyserial
┌──────────────┴──────────────────────────────────────────────────┐
│  Host PC (Python)                                               │
│  protocol.py (frame parser) → dashboard.py (curses UI + CSV)   │
└─────────────────────────────────────────────────────────────────┘
```

---

## Packet wire format

All multi-byte fields are little-endian. Maximum wire length is 247 bytes
(SX1276 LoRa payload limit with explicit header mode).

```
Offset  Size  Field        Description
──────  ────  ───────────  ────────────────────────────────────────────────
0       1     type         PKT_DATA=0x01  PKT_HELLO=0x02
                            PKT_ACK=0x03   PKT_NACK=0x04
1       1     src          address of the node that last relayed this packet
2       1     dst          address of the intended next hop (or BROADCAST)
3       1     origin       address of the node that first injected the packet
4       1     ttl          decremented each hop; packet dropped when 0
5       1     seq          per-origin counter, wraps at 255
6       1     hops         number of hops traversed (0 = direct)
7       1     payload_len  bytes of application payload that follow
8+      N     payload      application data (up to 239 bytes)
```

### Address space

| Value | Role |
|---|---|
| 0x00 | reserved |
| 0x01 | base node (ADDR_BASE) |
| 0x02–0xFE | sensor / relay nodes |
| 0xFF | broadcast (ADDR_BROADCAST) |

---

## Routing protocol

The protocol is **proactive distance-vector** — each node periodically
broadcasts its own address as a HELLO. Hearing a HELLO teaches the receiver
a route to the origin.

### Route advertisement (HELLO flood)

```
Node 5 broadcasts HELLO (src=5, origin=5, ttl=7, hops=0)
  │
  ├── Node 3 hears it directly
  │     stores: reach 5 via 5 with hop_count=1
  │     rebroadcasts (src=3, origin=5, ttl=6, hops=1)
  │       │
  │       └── Node 1 (base) hears the relay
  │             stores: reach 5 via 3 with hop_count=2
```

**Route selection policy** — a new route to a known destination is accepted
only when:
- no valid route to that destination exists, or
- the new hop_count is strictly lower (better path), or
- it comes from the same next_hop (refresh of an existing entry).

This prevents route flapping when a weaker relayed copy of a HELLO arrives
after the direct copy has already established a 1-hop route.

### Route expiry

Entries older than `ROUTE_EXPIRE_MS` (30 s) are silently ignored on lookup
and explicitly cleared by `rt_expire`, which is called once per HELLO cycle.
A node that stops transmitting HELLOs disappears from all routing tables
within 30 s.

### Duplicate suppression

The `DupTable` is a fixed-size circular buffer of `(origin, seq, type)`
triples. Before relaying any packet, a node checks whether it has already
processed the same triple; if so the packet is silently dropped.

The `type` field is included in the key so that a legitimate use of the same
origin+seq across different packet types (e.g. an ACK carrying the seq of the
DATA it confirms) does not cause false suppression.

The circular buffer silently evicts the oldest entry when full. With
`DUP_TABLE_SIZE=64` and a HELLO interval of 5 s across 32 nodes, the buffer
holds approximately 10 s of traffic before eviction begins.

---

## Data path (end-to-end)

```
Sensor node 5 wants to send temp/humidity to base node 1.

1. mesh_send(node=5, dst=1, payload)
     rt_lookup(dst=1) → next_hop=3
     emit PKT_DATA(src=5, dst=3, origin=5, ttl=7)

2. Node 3 receives PKT_DATA
     dst != 3 → forward
     rt_lookup(dst=1) → next_hop=1
     emit PKT_DATA(src=3, dst=1, origin=5, ttl=6, hops=1)

3. Base node 1 receives PKT_DATA
     dst == 1 → deliver to application callback
     on_recv(payload, len=13, src=5, rssi=-82, snr=6)
     emit PKT_ACK(src=1, dst=3, origin=1, payload=[5, seq])

4. Node 3 relays PKT_ACK toward origin 5
     rt_lookup(final_dest=5) → next_hop=5
     emit PKT_ACK(src=3, dst=5, origin=3)

5. Node 5 receives PKT_ACK
     final_dest == 5 → delivery confirmed
```

**No-route case** — if any relay node cannot find a route to the destination,
it emits a `PKT_NACK` broadcast. The NACK propagates back toward the originator
(also by TTL-limited broadcast) and triggers the `on_nack` application callback.

---

## SX1276 driver (`radio.cpp`)

The driver communicates with the SX1276 over SPI at 8 MHz. All register
accesses are raw reads/writes; no library is used.

### RF configuration defaults

| Parameter | Value |
|---|---|
| Frequency | 915 MHz (override with `RADIO_FREQ_HZ`) |
| Bandwidth | 125 kHz (BW_125K) |
| Spreading factor | SF7 |
| Coding rate | 4/5 |
| TX power | +17 dBm (PA_BOOST) |
| Preamble | 8 symbols |
| CRC | enabled |
| Header mode | explicit |

### TX flow

`radio_send` puts the chip into standby, resets the FIFO pointer, writes
the payload via SPI burst, sets DIO0 mapping to TxDone, enters TX mode
and polls DIO0. TX timeout is 3 s.

### RX flow

`radio_start_rx` enters continuous-receive mode (`MODE_RX_CONT`). DIO0 is
mapped to RxDone. `radio_packet_available` polls the DIO0 pin level.
`radio_recv` reads the IRQ flags register, checks the CRC error bit, reads
`REG_RX_NB_BYTES` and `REG_FIFO_RX_CURRENT_ADDR`, then burst-reads the FIFO.

RSSI correction: `rssi_dBm = RegPktRssiValue − 157` (HF port, Semtech
application note AN1200.13). When SNR < 0, the true signal RSSI is further
reduced by `SNR / 4` dB.

---

## Mesh engine (`mesh.cpp`)

`mesh_update` is called from `loop()` as fast as possible. Its two
responsibilities each cycle:

1. **HELLO timer** — if `now - last_hello_ms >= HELLO_INTERVAL_MS`, broadcast
   a fresh HELLO with incremented `seq`, then call `rt_expire` to remove stale
   routes.

2. **RX poll** — check `radio_packet_available`. If a packet is waiting, call
   `radio_recv`, validate the header length and drop own-origin packets, then
   call `_process_packet`.

`_process_packet` dispatches on `pkt->type`:

- **HELLO** — call `rt_update` with `hop_count = pkt->hops + 1`, then
  rebroadcast with `ttl--` and `hops++` if TTL > 1.
- **DATA** — if `dst == self`, deliver to `on_recv` and send ACK; otherwise
  forward via routing table or send NACK on no-route.
- **ACK** — if `final_dest == self`, consume silently; otherwise forward
  toward `final_dest`.
- **NACK** — invoke `on_nack`, rebroadcast with TTL--

---

## Sensor payload format (13 bytes)

```
Offset  Size  Field        Encoding
──────  ────  ───────────  ───────────────────────────────────
0       1     node_addr    redundant node address (cross-check)
1       4     temp_raw     int32 LE = temp_celsius × 100
5       4     hum_raw      int32 LE = humidity_pct × 100
9       4     uptime_s     uint32 LE = millis() / 1000
```

The base node decodes this and emits a JSON frame over USB serial.

---

## Host protocol (JSON over serial)

Five frame types emitted by the base node:

| type | Fields | When |
|---|---|---|
| `boot` | addr | On startup |
| `ready` | — | After `mesh_init` completes |
| `data` | src, rssi, snr, hops, temp_c, hum_pct, uptime_s | On DATA receipt |
| `status` | addr, routes, tx, rx, relay, drop_dup, drop_ttl, drop_no_route | Every 10 s |
| `nack` | dest, seq | On NACK receipt |

---

## Test coverage

### C tests (`tests/test_mesh.c`, native GCC, 32 assertions)

| Group | What is tested |
|---|---|
| T1 Routing table | Empty lookup, insert, refresh, better-hops replacement, worse-hops rejection, broadcast/none rejection, expiry, `rt_expire`, `rt_best_hop_count`, table-full overflow |
| T2 Duplicate table | First occurrence, same triple, different seq, different type, different origin, circular eviction, multi-origin coexistence |
| T3 Packet format | Wire-length macro, struct size, MESH_MAX_HOPS bound, DUP_TABLE_SIZE bound |
| T4 Protocol logic | Relayed HELLO establishes 2-hop route, direct HELLO replaces multi-hop, 30-entry table population |

### Python tests (`tests/test_host.py`, pytest, 14 assertions)

| Group | What is tested |
|---|---|
| parse_line | data / status / nack / boot / ready frames, empty string, invalid JSON, unknown type, whitespace stripping, rx_time currency |
| NodeStats | rssi_avg calculation, age_s property |
| Dashboard | Data accumulation into NodeStats, NACK counter increment |
