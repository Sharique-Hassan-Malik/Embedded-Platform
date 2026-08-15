# Wireless Sensor Mesh with Custom Routing Protocol

> Part of the [Embedded Platform](../../README.md) — nineteen firmware projects
> behind one build harness. This folder builds and runs on its own; `embed build
> --only sensor-mesh` builds it alongside the rest.

A multi-hop wireless sensor network using nRF24L01+ radios and a mesh routing
protocol implemented from scratch. Sensor nodes read temperature, humidity,
light and motion and forward data toward a base node over multiple hops. The
base node streams readings to a Python dashboard over USB serial.

---

## The Hard Part

Off-the-shelf mesh libraries (RF24Mesh, RF24Network) handle routing internally.
This project implements the routing protocol directly on top of the RF24 hardware
driver, which provides only raw 32-byte packet send and receive.

The protocol is a simplified AODV (Ad-hoc On-demand Distance Vector):

**Route discovery via RREQ/RREP flooding.** When a sensor node has no cached
path to the base, it broadcasts a route request (RREQ). Intermediate nodes
rebroadcast it if fresher than their cached path, and the destination replies
with a unicast route reply (RREP) that propagates back through the reverse path.
Each node along the way updates its routing table and caches the reverse path
for RREP forwarding — without needing a central controller or pre-configured
topology.

**Per-hop ACK with retry and route invalidation.** Rather than relying on the
nRF24L01's hardware auto-ACK (which uses fixed addressing and cannot span
multiple hops), each firmware hop waits for a software PKT_ACK from the next
node. On timeout the hop retries up to MAX_RETRIES times, then marks the route
invalid and triggers fresh route discovery. This keeps retransmissions local to
the broken hop rather than requiring end-to-end retransmission.

**Broadcast deduplication.** RREQ floods the network. Without deduplication,
each node would repeatedly rebroadcast the same RREQ causing exponential packet
growth. A circular 16-entry cache of `(origin, seq)` pairs suppresses duplicate
RREQ packets at each node.

**Single firmware binary for all node types.** Setting `NODE_ID` in `config.h`
at compile time selects base or sensor behaviour. The same `.ino` compiles
correctly for both — sensor-specific code (`sensor.h`, DHT22 reads, periodic
transmit) is conditionally included with `#if NODE_ID != 0`.

---

## Architecture

```
firmware/node/
  node.ino          — main sketch (base or sensor based on NODE_ID)
  config.h          — NODE_ID, pins, routing timing parameters
  packet.h          — 32-byte packet struct and type constants
  radio.h/.cpp      — RF24 wrapper: unicast, broadcast, receive
  router.h/.cpp     — AODV routing table, RREQ/RREP/ACK state machine
  sensor.h/.cpp     — DHT22, LDR and PIR reading

host/
  transport.py      — serial thread, line parser, NodeReading dataclass
  dashboard.py      — live Matplotlib dashboard (one row per node)
  requirements.txt
```

See `docs/ARCHITECTURE.md` for the full routing protocol state machine,
packet format table and data structure definitions.

---

## Hardware (per node)

| Component | Notes |
|---|---|
| Arduino Uno or Nano | ATmega328P |
| nRF24L01+ | 2.4 GHz, SPI, 3.3 V — add 10 µF decoupling cap |
| DHT22 | Temperature + humidity (sensor nodes) |
| LDR + 10 kΩ resistor | Light level voltage divider (sensor nodes) |
| HC-SR501 PIR | Motion detection (sensor nodes) |

See `docs/WIRING.md` for pin assignments, library installation and step-by-step
multi-node flashing instructions.

---

## Quickstart

### 1 — Install Arduino libraries

Library Manager → install:
- **RF24** by TMRh20
- **DHT sensor library** by Adafruit

### 2 — Flash each node

In `firmware/node/node.ino`, change `#define NODE_ID` to the target node's ID
(0 = base, 1–7 = sensor), then upload to that Arduino. Repeat for each node.

### 3 — Run the dashboard

```bash
pip install -r host/requirements.txt
python host/dashboard.py --port /dev/ttyACM0
```

The dashboard discovers nodes automatically as they send their first packets.

---

## Serial Protocol (base node)

```
READY
DATA,<origin>,<hops>,<temp_x10>,<hum_x10>,<light_adc>,<motion>
```

| Field | Encoding |
|---|---|
| `origin` | Sender node ID (1–7) |
| `hops` | Number of hops the packet traversed |
| `temp_x10` | Temperature × 10 as signed integer (234 = 23.4 °C); -999 = error |
| `hum_x10` | Humidity × 10 as unsigned integer (652 = 65.2 %); 65535 = error |
| `light_adc` | Raw ADC reading (0–1023) |
| `motion` | 0 or 1 |

---

## Configuration

All tuning parameters are in `config.h`:

| Parameter | Default | Effect |
|---|---|---|
| `NODE_ID` | 1 | Change per node before flashing |
| `RF_CHANNEL` | 76 | Must match across all nodes (0–125) |
| `SENSOR_PERIOD_MS` | 5000 | Sensor read interval per node |
| `MAX_RETRIES` | 3 | Per-hop retry count before route invalidation |
| `ACK_TIMEOUT_MS` | 80 | Per-hop ACK wait time |
| `ROUTE_EXPIRE_MS` | 60000 | Route entry lifetime |
| `RF_PA_LEVEL` | RF24_PA_LOW | Increase for longer range |

---

## Results

- Two-node direct link: first reading appears within 10 s of power-on
- Three-node mesh (1 relay hop): first relayed reading appears within 20 s
- Per-hop ACK latency: ~80 ms (ACK_TIMEOUT_MS)
- Total end-to-end latency at 2 hops: ~200 ms
- nRF24L01+ range at RF24_PA_LOW: 20–50 m open air

---

## File Map

| File | Purpose |
|---|---|
| `firmware/node/node.ino` | Main sketch — setup and loop |
| `firmware/node/config.h` | All per-node configuration |
| `firmware/node/packet.h` | Wire packet format |
| `firmware/node/radio.h/.cpp` | RF24 hardware abstraction |
| `firmware/node/router.h/.cpp` | AODV routing protocol |
| `firmware/node/sensor.h/.cpp` | DHT22 + LDR + PIR |
| `host/transport.py` | Serial parser and NodeReading dataclass |
| `host/dashboard.py` | Live multi-node Matplotlib dashboard |
| `docs/ARCHITECTURE.md` | Protocol state machine and packet format |
| `docs/WIRING.md` | BOM, wiring diagrams and flashing guide |
