# Architecture

## Overview

A passive logic-analyzer-lite that simultaneously captures UART, I2C and SPI
traffic without any electrical connection to the target's power supply or
ground.  All three sniffers run concurrently on a PIC24FJ64GA002 using
hardware peripherals and change-notification interrupts.  Captured events are
timestamped to ±0.5 µs, streamed as human-readable ASCII frames to a host PC
at 115 200 baud, and optionally logged to a raw SD card for offline analysis.
A Python host tool provides a live packet-level decode view and can reconstruct
I2C transactions, SPI transfers and UART messages from either the live stream
or the SD card image.

---

## System Block Diagram

```
Target under test
──────────────────────────────────────────────────────────────────
  UART TX ─────► RB6 (RP6/U2RX)    UART2 RX ISR
  I2C SCL ─────► RB8 (CN6)   ┐
  I2C SDA ─────► RB9 (CN7)   ┘  CN ISR (shared with SPI /CS)
  SPI SCK ─────► RB10 (RP10/SCK1) ┐
  SPI MOSI ────► RB11 (RP11/SDI1) ┤  SPI1 slave RX ISR
  SPI MISO ────► RB12 (GPIO)      ┘  (MISO sampled in SPI1 ISR)
  SPI /CS ─────► RB13 (CN11)          CN ISR
──────────────────────────────────────────────────────────────────
                        │ all ISRs push to capture_buf[]
                        ▼
               cbuf_t capture_buf   (256-entry circular buffer)
                        │
          main loop drains at UART speed
                   │                 │
                   ▼                 ▼
         host_uart_print_word()   sdlog_write()
               │                      │
               ▼                      ▼
         UART1 115 200 baud       SD card raw sectors
               │
               ▼
         host/decode.py
         (live or sd mode)
```

---

## Interrupt Priority Map

| Priority | Source | Latency budget |
|---|---|---|
| 7 | Timer1 overflow (timestamp) | < 1 µs |
| 6 | CN (I2C SCL/SDA + SPI /CS) | < 1.25 µs (400 kHz I2C half-bit) |
| 5 | UART2 RX (UART sniffer) | < 87 µs (115 200 baud bit time) |
| 4 | SPI1 RX (SPI sniffer) | < 1 µs (clock-latched) |
| 3 | UART1 RX (control channel) | best-effort |

Higher numeric priority preempts lower.  The Timer1 interrupt fires before
any other to maintain timestamp accuracy.

---

## Module Descriptions

### `sniff_uart.c` — UART2 receive-only sniffer

UART2 is configured with DISSDO (transmit disabled, receive only) and mapped
to the sniffer input pin via PPS.  The RX interrupt fires on each received
byte and pushes a `PROTO_UART` word to the capture buffer.  The FERR bit from
U2STA is sampled at interrupt time and encoded as `FLAG_UART_FRAMING_ERR`.
Overrun (OERR) is cleared immediately to re-enable the receiver.

A baud rate lookup table covers 9 600 to 500 000 baud using `BRGH = 1`
(high-speed mode) for better accuracy.  The control channel command `B<n>`
reinitialises UART2 at runtime without a power cycle.

### `sniff_i2c.c` — Change-notification I2C bit-bang sniffer

The I2C protocol does not map onto any PIC24 hardware peripheral in sniff
mode; it is decoded entirely in software from SCL and SDA edge events.

The CN interrupt fires on any edge of RB8 (SCL) or RB9 (SDA).  On entry
both pins are sampled immediately and compared against their previous levels:

- **START**: SDA falls while SCL is high → push `FLAG_I2C_START`, reset state.
- **STOP**: SDA rises while SCL is high → push `FLAG_I2C_STOP`, return to idle.
- **Rising SCL**: sample SDA for the current bit.  After 8 bits, transition to
  the ACK-sampling state.  On the 9th rising edge, sample ACK/NACK and push
  the complete byte word.

The state machine handles:
- Address bytes (flagged `FLAG_I2C_ADDR`), including the R/W bit.
- Data bytes with ACK and NACK.
- Repeated STARTs (a new START resets the state machine mid-transaction).

**Timing constraint**: at 400 kHz I2C the minimum SCL half-period is 1.25 µs.
At 16 MHz Fcy the ISR entry takes ≈ 10 cycles (0.63 µs) and the body
completes in ≈ 25 cycles (1.56 µs total), which fits within the 1.25 µs
half-period only because PIC24's CN interrupt has no pipelining penalty and
the priority-7 Timer1 interrupt does not preempt it.  In practice the sniffer
works reliably at 400 kHz.  For fast-mode plus (1 MHz) a faster MCU clock or
a hardware I2C module with a dedicated buffer would be required.

### `sniff_spi.c` — SPI1 hardware slave sniffer

SPI1 is configured in slave mode with `DISSDO = 1` (output pin disabled) and
`SSEN = 1` (/SS hardware control).  The PPS maps the target MOSI line to
SPI1's SDI input.  When a full 8-bit byte is clocked in, the SPI1 interrupt
fires.

MISO is not connected to any SPI1 hardware input because SPI1 has only one
shift register.  Instead, MISO is sampled at each SPI1 interrupt by reading
the raw GPIO level of RB12 and shifting it into an 8-bit shadow register
(`miso_shift`).  After 8 interrupt events (one per byte) the reconstructed
MISO byte is emitted.  This approach works for SCK ≤ 2 MHz where the
assumption that one MISO bit was clocked per SPI1 byte interrupt holds.

/CS edges are detected by the CN interrupt (CN11 on RB13) which calls
`sniff_spi_cs_check()` to emit `FLAG_SPI_CS_ASSERT` and
`FLAG_SPI_CS_DEASSERT` events.

### `sdlog.c` — Raw-sector SD logger

Identical in concept to the weather station SD logger (project 51):
512-byte sectors, 32 records of 16 bytes each.  Each record stores the
32-bit timestamp and 32-bit capture word.  No FAT filesystem.

### `host_uart.c` — ASCII frame serialiser

Converts packed capture words to human-readable one-line frames:

| Wire event | Output line |
|---|---|
| UART 0x41 | `00001234 U 41` |
| I2C START | `00001240 I S` |
| I2C ADDR 0x48 W ACK | `00001260 I A 48 W ACK` |
| I2C DATA 0xFF NACK | `00001280 I D FF NAK` |
| I2C STOP | `000012A0 I P` |
| SPI /CS LOW | `00001300 S CS` |
| SPI MOSI 0x9F | `00001320 S O 9F` |
| SPI MISO 0x1D | `00001322 S I 1D` |
| SPI /CS HIGH | `00001340 S CD` |

The 8-hex-digit timestamp prefix is the 32-bit microsecond counter.

### `host/decode.py` — Host Python decoder

**Live mode** reads the ASCII stream from the serial port and passes each line
through `parse_serial_line()` to reconstruct capture words and `Frame`
objects.  A `PacketReconstructor` accumulates frames and emits logical
packets:

- **I2C**: START → ... → STOP is one transaction.
- **SPI**: /CS LOW → ... → /CS HIGH is one transfer.
- **UART**: consecutive bytes within a 5 ms inter-byte gap form one message.

Each completed packet is printed with hex dump and ASCII representation.

**SD mode** reads raw sectors from the SD card image, decodes all 16-byte
records into `Frame` objects, feeds them through the same
`PacketReconstructor`, and writes all decoded packets to a text file.

---

## Capture Word Format

```
 31    28 27    24 23        8 7          0
┌────────┬────────┬───────────┬───────────┐
│ proto  │ flags  │  metadata │   data    │
│  4 b   │  4 b   │   16 b    │   8 b     │
└────────┴────────┴───────────┴───────────┘

proto:  1 = UART,  2 = I2C,  3 = SPI
flags:  see hw.h FLAG_* constants
meta:   unused in current firmware (reserved for multi-byte metadata extensions)
data:   the captured byte, or 0 for events without a data payload (START/STOP/CS)
```

---

## Hardware Schematic

```
PIC24FJ64GA002                        Target under test
──────────────────────────────────────────────────────
RB6  (RP6)  ←── 1 kΩ ──── Target TX
RB8         ←── 1 kΩ ──── Target I2C SCL
RB9         ←── 1 kΩ ──── Target I2C SDA
RB10 (RP10) ←── 1 kΩ ──── Target SPI SCK
RB11 (RP11) ←── 1 kΩ ──── Target SPI MOSI
RB12        ←── 1 kΩ ──── Target SPI MISO
RB13        ←── 1 kΩ ──── Target SPI /CS

Series 1 kΩ resistors on all sniffer inputs provide short-circuit
protection and voltage-division if target logic levels differ.
The PIC24 has 5 V-tolerant inputs on PORTB — no level shifter needed
for 3.3 V or 5 V targets.

RB2  (RP2)  ──── USB-serial adapter RX  (host UART TX, 115 200 baud)
RB3         ←─── USB-serial adapter TX  (control channel)
RB4         ←─── SD card MISO
RB14 (RP14) ──── SD card SCK
RB15 (RP15) ──── SD card MOSI
RB5         ──── SD card /CS

RA0         ──── 330 Ω ──── LED ──── GND
VDD = 3.3 V, 100 nF decoupling per VDD pin
```

---

## File Map

| File | Description |
|---|---|
| `firmware/hw.h` | Pin map, capture word format, circular buffer |
| `firmware/sniff_uart.c/.h` | UART2 passive RX sniffer |
| `firmware/sniff_i2c.c/.h` | CN-driven I2C bit-bang sniffer |
| `firmware/sniff_spi.c/.h` | SPI1 slave + MISO GPIO shadow sniffer |
| `firmware/sdlog.c/.h` | Raw-sector SD logger |
| `firmware/host_uart.c/.h` | ASCII frame serialiser and host output |
| `firmware/main.c` | Timer1 timestamp, main drain loop, control channel |
| `host/decode.py` | Live decode view and SD card log extractor |
| `docs/ARCHITECTURE.md` | This document |
