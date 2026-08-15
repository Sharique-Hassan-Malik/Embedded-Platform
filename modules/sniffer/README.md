# PIC24 Protocol Sniffer

> Part of the [Embedded Platform](../../README.md) — nineteen firmware projects
> behind one build harness. This folder builds and runs on its own; `embed build
> --only sniffer` builds it alongside the rest.

A passive, simultaneous UART/I2C/SPI protocol sniffer built on the
PIC24FJ64GA002.  All three buses are captured concurrently without any
electrical connection to the target's power supply.  Captured events are
timestamped to ±0.5 µs via a hardware timer, streamed as ASCII frames to a
host PC at 115 200 baud, and optionally logged to a raw SD card for offline
analysis.  A Python host tool provides a live packet-level decode view and can
reconstruct I2C transactions, SPI transfers and UART messages from either the
live stream or an SD card image.

---

## What it does

Wire the sniffer inputs in parallel with the target bus lines (six inputs plus
a 1 kΩ series resistor on each — no other hardware required).  Power the
sniffer from USB.  Connect a USB-serial adapter to the host UART output.  Open
`decode.py live` and the terminal immediately shows decoded traffic:

```
── I2C transaction ─────────────────────────────
  00012300.000 ms  I2C   START
  00012301.250 ms  I2C   ADDR 0x48 W ACK
  00012302.500 ms  I2C   DATA 0xF3 ACK
  00012303.750 ms  I2C   STOP
  hex: f3
  ascii: .

── SPI transaction ─────────────────────────────
  00015000.000 ms  SPI   /CS LOW
  00015001.600 ms  SPI   MOSI 0x9F '.'
  00015001.601 ms  SPI   MISO 0x1D '.'
  00015003.200 ms  SPI   /CS HIGH
  MOSI: 9f
  MISO: 1d
```

---

## The hard part

**I2C in software at 400 kHz with a CN interrupt.**  The I2C protocol cannot
be mapped onto any PIC24 peripheral in passive mode.  The bit-bang sniffer
must recognise START, STOP, data bits and ACK/NACK from raw SCL/SDA edge
events in a CN ISR that must complete within one SCL half-period (1.25 µs at
400 kHz).  The state machine (five states, one switch statement) executes in
about 25 cycles (1.56 µs at 16 MHz Fcy), fitting the budget because CN is
given interrupt priority 6 and the only higher-priority interrupt (Timer1
overflow at priority 7) fires at most once per 65.5 ms.

**Concurrent capture without losing data.**  Three independent interrupt
sources (UART2 RX, CN, SPI1 RX) all push to a single 256-entry circular
buffer without a mutex.  This works because each ISR writes only `cbuf_push()`
which is a single-instruction `tail = (tail+1) & mask` — atomic on PIC24 as
long as `tail` is a 16-bit aligned variable.  On overflow the oldest entry is
discarded rather than stalling an ISR.

**MISO capture without a second SPI module.**  SPI1 has one shift register and
can receive only one signal at a time.  MISO is captured by sampling the raw
GPIO level of the MISO pin inside the SPI1 interrupt (which fires once per
byte) and assembling the 8 samples into a shadow byte.  This is accurate as
long as SCK ≤ 2 MHz, because each SPI1 interrupt corresponds to exactly one
MISO bit having been clocked past the pin.

---

## Architecture

See `docs/ARCHITECTURE.md` for the interrupt priority map, detailed module
descriptions, capture word bit layout, timing derivations and a hardware
schematic.

---

## Hardware

| Component | Notes |
|---|---|
| PIC24FJ64GA002 | 28-pin, 16 MHz, 5 V-tolerant PORTB inputs |
| 1 kΩ resistors × 6 | Series protection on all sniffer inputs |
| Micro SD card | Any capacity; FAT not required |
| USB-serial adapter | For host UART output at 115 200 baud |

**Pin map**

| PIC24 pin | Signal | Purpose |
|---|---|---|
| RB6 (RP6) | SNIFF_UART_RX | Target TX (passive) |
| RB8 | SNIFF_I2C_SCL | Target SCL (passive) |
| RB9 | SNIFF_I2C_SDA | Target SDA (passive) |
| RB10 (RP10) | SNIFF_SPI_SCK | Target SCK (passive) |
| RB11 (RP11) | SNIFF_SPI_MOSI | Target MOSI (passive) |
| RB12 | SNIFF_SPI_MISO | Target MISO (passive) |
| RB13 | SNIFF_SPI_CS | Target /CS (passive) |
| RB2 (RP2) | HOST_TX | 115 200 baud output to PC |
| RB3 | HOST_RX | Control channel from PC |
| RB14/RB15/RB4/RB5 | SD card | SPI2 + /CS |

---

## Host tool

```bash
pip install pyserial

# Live decode
python3 host/decode.py live --port /dev/ttyUSB0

# Set sniffer UART baud to 9600
python3 host/decode.py live --port /dev/ttyUSB0 --sniff-baud 9600

# Set SPI mode 3 (CPOL=1 CPHA=1)
python3 host/decode.py live --port /dev/ttyUSB0 --spi-mode 3

# Verbose: print every raw frame plus packets
python3 host/decode.py live --port /dev/ttyUSB0 --verbose

# Decode SD card log offline
python3 host/decode.py sd --image /dev/sdX --out capture.txt
python3 host/decode.py sd --image sd_dump.bin --out capture.txt
```

---

## Control channel (host → sniffer)

| Command | Effect |
|---|---|
| `B0`–`B6` | Set UART sniffer baud: 9600 / 19200 / 38400 / 57600 / 115200 / 250000 / 500000 |
| `M0` | SPI mode 0 (CPOL=0, CPHA=0) |
| `M3` | SPI mode 3 (CPOL=1, CPHA=1) |
| `S` | Stop SD card logging |
| `G` | Resume SD card logging |
| `R` | Reset timestamp counter to zero |

Each command is acknowledged with `>` on the next output line.

---

## Building

1. Open MPLAB X, create a project for PIC24FJ64GA002.
2. Select XC16 as the toolchain.
3. Add all `.c` files in `firmware/` to Source Files.
4. Add all `.h` files to Header Files.
5. Set optimisation to `-O1`.
6. Build and program with PICkit 3/4 or SNAP.

---

## Results

| Metric | Value |
|---|---|
| Simultaneous buses | 3 (UART + I2C + SPI) |
| Timestamp resolution | 0.5 µs |
| Max I2C clock | 400 kHz (fits in CN ISR at 16 MHz) |
| Max SPI clock (MISO shadow) | 2 MHz |
| Max UART baud | 500 000 |
| Capture buffer depth | 256 events |
| Host output baud | 115 200 |
| SD log record size | 16 bytes (32 records per sector) |
| SD log capacity | ~65 000 sectors = ~2 million events |
