# Architecture

## System Overview

```
  Sensor Node 1 ──┐
                  │  nRF24L01
  Sensor Node 2 ──┤  mesh
                  │  (multi-hop)
  Sensor Node 3 ──┘
        │
        ▼
   Base Node 0
   (USB serial)
        │
        ▼
  Python host
  transport.py  →  dashboard.py
```

Each sensor node reads temperature/humidity (DHT22), light (LDR) and motion
(PIR) and sends a DATA packet toward the base node every 5 seconds.
Nodes that are not in direct radio range of the base relay packets through
intermediate nodes.

---

## Packet Format

The nRF24L01 supports a maximum payload of 32 bytes.
Every packet uses all 32 bytes in a fixed layout:

```
Byte  0   dest        destination node ID (0 = base, 0xFF = broadcast)
Byte  1   src         immediate sender (changes at each hop)
Byte  2   origin      packet creator (never changes during forwarding)
Byte  3   seq         per-origin sequence counter (wraps 0–255)
Byte  4   flags       [7:4] TTL remaining   [3:0] packet type
Byte  5   hop_count   incremented at each hop
Bytes 6–31 payload   26-byte type-specific content
```

Packet types:

| Value | Name | Direction | Purpose |
|---|---|---|---|
| 0x01 | DATA | sensor → base | Sensor readings |
| 0x02 | RREQ | broadcast | Route request — find path to dest |
| 0x03 | RREP | unicast | Route reply — path found |
| 0x04 | ACK | unicast | Per-hop acknowledgement |
| 0x05 | NACK | unicast | Negative ACK (no route available) |

---

## Routing Protocol

The protocol is a simplified AODV (Ad-hoc On-demand Distance Vector).
Metric: hop count. Route maintenance: on-demand with expiry.

### Route Discovery

```
Node A wants to reach Node D — no route cached:

  A ──broadcast──► RREQ{dest=D, origin=A, seq=N, hop=0}
       │
       ├──► B  (rebroadcasts if fresher than cached)
       │         RREQ{dest=D, origin=A, seq=N, hop=1}
       │                   │
       │                   └──► D  (destination found)
       │                             sends RREP back via reverse path
       │
       └──► C  (has route to D, metric=1)
                 sends gratuitous RREP on D's behalf

  D ──unicast──► RREP{rrep_dest=D, metric=0} → B → A
  B stores: route(D) = {next=D, hops=1}
  A stores: route(D) = {next=B, hops=2}
```

Each node that forwards RREQ also records a reverse path entry:
`origin → prev_hop`, so RREP can be routed back without needing to
know the full topology.

### Data Forwarding with Per-Hop ACK

```
A ──DATA──► B ──ACK──► A   (hop 1 confirmed)
B ──DATA──► D ──ACK──► B   (hop 2 confirmed)

On no ACK within ACK_TIMEOUT_MS:
  retry up to MAX_RETRIES times
  then invalidate route entry
  then broadcast fresh RREQ
```

Per-hop ACK means a retry is handled locally at each hop rather than
end-to-end. A two-hop path where the second hop is unreliable only
retransmits on that hop — node A never retransmits the full packet.

### Deduplication

Each node maintains a circular cache of `(origin, seq)` pairs of size
`DEDUP_TABLE_SIZE` (16). Any RREQ matching a cached pair is silently
dropped, preventing broadcast storms.

### Route Expiry

Route entries older than `ROUTE_EXPIRE_MS` (60 s) are removed.
This prevents forwarding through stale paths after network topology changes.

---

## nRF24L01 Addressing

```
Pipe 0 (broadcast, all nodes listen):  0xF0F0F0F0FF
Pipe 1 (unicast for node N):           0xF0F0F0F0NN
```

Hardware auto-ACK is disabled. The routing layer's PKT_ACK replaces it,
giving the protocol full control over retry timing and count.

---

## Serial Protocol (Base Node → Python)

All messages are newline-terminated ASCII at 115200 baud.

```
READY              — emitted on startup
DATA,<origin>,<hops>,<temp_x10>,<hum_x10>,<light_adc>,<motion>
ERR,<reason>       — radio init failure or parse error
```

Field encoding:
- `temp_x10`: temperature × 10 as signed integer (234 = 23.4 °C); -999 = sensor error
- `hum_x10`:  humidity × 10 as unsigned integer (652 = 65.2 %); 65535 = sensor error
- `light_adc`: raw ADC reading 0–1023
- `motion`: 0 or 1

---

## Routing Table Data Structures

```
RouteEntry (per destination):
  dest          — destination node ID
  next_hop      — which node to forward to
  metric        — hop count
  dest_seq      — destination's latest known sequence number (freshness)
  updated_at    — millis() timestamp for expiry
  valid         — whether this entry is in use

ReverseEntry (for RREP routing):
  origin        — who originated the RREQ
  prev_hop      — who forwarded the RREQ to us (send RREP back here)
  created_at    — for cleanup

DedupEntry (broadcast deduplication):
  origin        — packet origin
  seq           — sequence number
```

---

## Software Layers

### Firmware (Arduino C++)

| File | Role |
|---|---|
| `config.h` | NODE_ID, pins, routing parameters |
| `packet.h` | 32-byte packet struct, type constants, payload layouts |
| `radio.h/.cpp` | RF24 wrapper — send/broadcast/receive |
| `router.h/.cpp` | AODV routing, routing table, ACK state machine |
| `sensor.h/.cpp` | DHT22 + LDR + PIR reading |
| `node.ino` | Main sketch — compiles as base or sensor via NODE_ID |

### Python Host

| File | Role |
|---|---|
| `transport.py` | Serial reader thread, line parsing, NodeReading dataclass |
| `dashboard.py` | Matplotlib live dashboard — one row per node |
| `requirements.txt` | pyserial, matplotlib |
