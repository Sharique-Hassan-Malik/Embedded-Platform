# LoRa Mesh Network

Multi-hop mesh network using SX1276 LoRa modules on Arduino. Sensor nodes relay packets across any number of hops to a base node connected to a PC. A Python dashboard displays live readings, signal strength and routing statistics.

No RadioHead. No LoRa.h. The radio driver, routing protocol and mesh engine are written from scratch.

## What it demonstrates

- Distance-vector routing with proactive HELLO flooding
- Multi-hop data forwarding with TTL-limited rebroadcast
- Duplicate suppression via circular (origin, seq, type) cache
- Priority-ordered route selection — lower hop count wins
- ACK and NACK propagation across multiple hops
- SX1276 register-level SPI driver
- JSON telemetry over USB serial with a live curses dashboard

## Hardware

Each node requires:
- Arduino Uno or Nano
- SX1276 LoRa module (Ra-02, RFM95W or equivalent)
- DHT22 temperature/humidity sensor (sensor nodes only)

Wiring (all nodes):

| SX1276 | Arduino |
|---|---|
| NSS | D10 |
| SCK | D13 |
| MOSI | D11 |
| MISO | D12 |
| RST | D9 |
| DIO0 | D2 |

DHT22 data pin → D4 (sensor nodes only).

## Project structure

```
lora-mesh/
├── firmware/src/
│   ├── packet.h         wire format constants and Packet struct
│   ├── routing.h        distance-vector routing table (header-only)
│   ├── dup_table.h      duplicate suppression circular buffer (header-only)
│   ├── radio.h          SX1276 driver interface
│   ├── radio.cpp        SX1276 register-level SPI driver
│   ├── mesh.h           mesh protocol engine interface
│   ├── mesh.cpp         mesh engine — HELLO flood, forwarding, ACK/NACK
│   ├── node.ino         sensor node sketch (DHT22 + mesh send)
│   └── base_node.ino    base node sketch (mesh receive + JSON over serial)
├── host/
│   ├── protocol.py      JSON frame parser + background serial reader
│   └── dashboard.py     live curses dashboard + CSV logger
├── tests/
│   ├── test_mesh.c      32 C unit tests (routing, dup, packets, protocol)
│   └── test_host.py     14 Python tests (parser, stats, dashboard)
├── docs/
│   └── architecture.md
├── requirements.txt
└── README.md
```

## Flashing the firmware

Open the Arduino IDE and load the appropriate sketch:

- **Sensor node**: open `firmware/src/node.ino`. Set `NODE_ADDR` to a unique value (0x02–0xFE) for each node. Flash to the Arduino.
- **Base node**: open `firmware/src/base_node.ino`. Flash to the Arduino connected to the PC.

All source files in `firmware/src/` must be present in the same sketch folder.

## Running the dashboard

```bash
pip install -r requirements.txt
python host/dashboard.py --port /dev/ttyACM0

# Demo mode — no hardware required
python host/dashboard.py --demo

# With CSV logging
python host/dashboard.py --port /dev/ttyACM0 --log captures/run.csv
```

Dashboard keys: `q` quit, `r` reset statistics.

## Running the tests

C tests (no cross-compiler needed):

```bash
gcc -std=c99 -Wall -Wextra -I firmware/src tests/test_mesh.c -o tests/test_mesh
./tests/test_mesh
```

Python tests:

```bash
pytest tests/test_host.py -v
```

## Configuration

Edit `firmware/src/packet.h` to adjust protocol parameters:

| Constant | Default | Description |
|---|---|---|
| `RADIO_FREQ_HZ` | 915000000 | Centre frequency in Hz |
| `MESH_MAX_HOPS` | 7 | Initial TTL / maximum hop depth |
| `MESH_MAX_NODES` | 32 | Routing table capacity |
| `HELLO_INTERVAL_MS` | 5000 | HELLO beacon period |
| `ROUTE_EXPIRE_MS` | 30000 | Stale route timeout |
| `DUP_TABLE_SIZE` | 64 | Duplicate suppression cache depth |

For 868 MHz (EU) define `RADIO_FREQ_HZ 868000000UL` in `radio.h` or pass it as a compiler flag.

## Protocol summary

Every node broadcasts a HELLO every 5 s. Neighbours relay the HELLO with `ttl--` and `hops++`, spreading route information through the mesh. When a sensor node wants to send data to the base, it looks up the next hop in its routing table and sends a DATA packet. Each relay forwards the DATA toward the base. If a relay has no route to the destination it sends a NACK broadcast back toward the sender.

Route updates follow the minimum-hop-count metric. A route to a known destination is replaced only when a strictly shorter path is discovered. Routes expire after 30 s of silence from the origin.

Duplicate packets are suppressed using a 64-entry circular buffer keyed on (origin, seq, type). A node will not relay the same packet more than once regardless of how many paths it arrives on.

## Range and packet loss

With SF7 and 125 kHz bandwidth at 915 MHz and +17 dBm TX power, direct range is typically 300–800 m in urban environments and 1–3 km line-of-sight. Each mesh hop extends effective range by this factor. Measured packet loss at -100 dBm RSSI (near link budget limit) is typically under 5 % with LoRa CRC enabled.

## References

- SX1276/77/78/79 Datasheet Rev 7 — Semtech
- Semtech Application Note AN1200.13 — LoRa Modem Designer's Guide
- SX1276 RSSI correction — Semtech AN1200.22
