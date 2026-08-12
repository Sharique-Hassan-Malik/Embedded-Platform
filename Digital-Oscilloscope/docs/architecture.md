# Architecture

## System overview

```
┌─────────────────────────────────────────────────┐
│  Raspberry Pi Pico (RP2040)                     │
│                                                 │
│  GPIO26/27/28 ──▶ ADC0/1/2                      │
│                     │                           │
│               ADC FIFO (DMA request)            │
│                     │                           │
│            DMA chan 0 ◀──▶ DMA chan 1            │
│            (ping-pong)                          │
│                     │                           │
│          uint16 buf[2][2048]                    │
│                     │                           │
│            main loop reads ready half           │
│                     │                           │
│          USB CDC (TinyUSB)                      │
└────────────────────────│────────────────────────┘
                         │  USB full-speed
┌────────────────────────│────────────────────────┐
│  Python host           ▼                        │
│                                                 │
│   SerialReader (background thread)              │
│     pyserial → sync hunt → CRC check            │
│     → Frame queue                               │
│                     │                           │
│   OscilloscopeApp (Tk main thread)              │
│     ├── Toolbar: start/stop/rate/channel        │
│     ├── Waveform canvas (matplotlib TkAgg)      │
│     │     └── trigger → windowed display        │
│     └── Measurements panel                      │
└─────────────────────────────────────────────────┘
```

---

## Firmware (`firmware/`)

### ADC configuration

The RP2040 ADC runs from a 48 MHz USB PLL clock.  The clock divisor sets the
sample rate:

```
sample_rate = 48_000_000 / clkdiv
```

The minimum divisor is 96 (500 ksps); the default is 960 (50 ksps).
`adc_set_clkdiv` takes a float — integer divisors are used throughout
to keep the rate exact.

The ADC is configured in free-running mode on a single input (channel
select is static).  The FIFO is enabled with `dreq_en = true` so every
completed sample generates a DMA request.

### Ping-pong DMA (`adc_dma.c`)

Two DMA channels are allocated and cross-chained:

```
chan 0 fills buf[0] → completion IRQ → arm chan 0 again → chain triggers chan 1
chan 1 fills buf[1] → completion IRQ → arm chan 1 again → chain triggers chan 0
```

Each IRQ handler sets `ready_half` to the index of the completed buffer.
The main loop reads `ready_half`, snapshots it to a local variable and
clears it — this is safe because the IRQ can only write it and the main
loop is the only reader, with no preemption between the two on core 0.

If the main loop doesn't drain `ready_half` before the next IRQ fires,
the `overflow` flag is set and propagated in the frame header.

### Frame builder

Frames are assembled directly into a static `_frame_buf` array:

```
[0xDE 0xAD 0xC0 0xDE] [channel] [flags] [n_samples LE16]
[sample_0 LE16] … [sample_{n-1} LE16]
[CRC16 LE16]
```

Samples are stored as 16-bit values with the 12-bit ADC result
left-aligned in bits `[15:4]` (bits `[3:0]` are zero).  The host
right-shifts by 4 on parse to recover the 12-bit value.

### Command parser

The main loop reads single bytes from the CDC interface.  The first byte
of each command is looked up in `_payload_len` to determine how many
following bytes to accumulate before dispatching.  This handles variable-
length commands without state-machine complexity.

### USB CDC

TinyUSB in device mode provides one CDC-ACM virtual serial port.  The
stdio USB bridge is disabled (`pico_enable_stdio_usb 0`) so the full
CDC bandwidth is available for sample data.  `tud_task()` is called once
per main loop iteration before any frame emission.

---

## Host (`host/`)

### `protocol.py`

Mirrors all constants from `firmware/include/protocol.h` and provides
CRC-16/CCITT-FALSE (verified against the standard "123456789" → 0x29B1
test vector) and command builder functions.

### `serial_reader.py` — `SerialReader`

Runs in a daemon thread.  The parser keeps a rolling bytearray and:

1. Scans for the 4-byte sync word `0xDE 0xAD 0xC0 0xDE`.
2. Reads the 8-byte header to determine `n_samples`.
3. Waits until the full frame (header + payload + CRC) is buffered.
4. Verifies CRC-16; discards and resyncs on mismatch.
5. Converts raw uint16 to millivolt float32 array.
6. Places a `Frame` dataclass on a bounded queue (newest frame wins on overflow).

Bytes discarded before sync or on CRC failure are counted in `drop_count`.

### `signal_proc.py`

**Trigger** — `find_trigger` computes a boolean array `above = samples >= level_mv`
and finds the first index where the edge direction condition holds.
`pre_samples` backs up the window so the trigger point is visible in context.
Free-running fallback returns index 0.

**Measurements** — all functions accept a millivolt float32 array:

| Function | Method |
|---|---|
| Vmin, Vmax, Vpp | `np.min`, `np.max`, `np.ptp` |
| Vmean | `np.mean` |
| Vrms | RMS of AC-coupled signal (mean-subtracted) |
| Frequency | Zero-crossing rate: `f = crossings / (2 × duration)` |
| Duty % | Fraction of samples ≥ threshold |

### `oscilloscope.py` — `OscilloscopeApp`

Tk main window with three regions:

**Toolbar** — comboboxes and radio buttons send protocol commands via
`SerialReader.send`.  Trigger level can also be dragged on the waveform
with the mouse (matplotlib button_press / motion_notify / button_release).

**Waveform canvas** — matplotlib figure embedded via `FigureCanvasTkAgg`.
`_render` is called on every frame from a `after(33, ...)` loop.  It
applies the trigger, slices the window, updates `Line2D` data and calls
`draw_idle()` for deferred redraw.  No full figure recreation occurs —
only the line data and axis limits update.

**Measurements panel** — right sidebar with a Tk `Label` per measurement,
updated synchronously in `_render`.

---

## Wire protocol

All fields are little-endian.

### Host → Pico

| Byte | Command | Payload |
|---|---|---|
| 0x01 | CMD_START | none |
| 0x02 | CMD_STOP | none |
| 0x03 | CMD_SET_RATE | uint32 clkdiv |
| 0x04 | CMD_SET_CHANNEL | uint8 channel (0–3) |
| 0x05 | CMD_SET_SAMPLES | uint16 n_samples |
| 0x06 | CMD_PING | none |

### Pico → Host

```
[0xDE 0xAD 0xC0 0xDE] [channel u8] [flags u8] [n_samples u16]
[sample_0 u16] … [sample_{n-1} u16] [CRC16 u16]
```

Flags: bit 0 = overflow (DMA buffer overrun since last frame).

Pong (CMD_PING response): `[0xAC] [version u32 LE]`.

### CRC-16/CCITT-FALSE

- Polynomial: 0x1021
- Initial value: 0xFFFF
- No input or output reflection
- No final XOR

Covers all bytes from the first sync byte through the last sample byte.
