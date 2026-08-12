# Architecture

## Overview

The system has two independent layers that communicate through the binary wire
protocol defined in `trace_protocol.h`.

```
┌─────────────────────────────────────────────────┐
│  Firmware (ARM Cortex-M / FreeRTOS)             │
│                                                 │
│  FreeRTOS trace macros                          │
│       │  (traceTASK_SWITCHED_IN, etc.)          │
│       ▼                                         │
│  trace_hooks.c                                  │
│       │  pack 8-byte frames into ring buffer    │
│       ▼                                         │
│  Trace_Flush() → UART TX                        │
└──────────────┬──────────────────────────────────┘
               │  921600 baud, 8N1
               ▼
┌─────────────────────────────────────────────────┐
│  Host (Python)                                  │
│                                                 │
│  SerialTransport / FileReplayTransport          │
│       │  raw bytes                              │
│       ▼                                         │
│  Decoder                                        │
│       │  TraceEvent list                        │
│       ▼                                         │
│  build_model()  →  SchedulingModel              │
│       │                                         │
│       ├── render_html()  →  gantt.html          │
│       └── render()       →  terminal            │
└─────────────────────────────────────────────────┘
```

## Firmware Layer

### Ring Buffer

`trace_hooks.c` maintains a 512-byte power-of-two ring buffer of raw trace bytes.
The `ring_push()` function is called from within FreeRTOS trace macros, which can
fire in interrupt context or with the scheduler suspended. It drops bytes silently
on overflow rather than blocking — this is the correct behavior for a trace system
that must not interfere with the application being observed.

`Trace_Flush()` is called from a dedicated low-priority task (or the idle hook)
and drains bytes to `Trace_UART_Send()` one byte at a time. Separating the push
and flush paths means the trace interrupt handler never waits for UART.

### Task Slot Registry

FreeRTOS task handles are opaque pointers. The firmware maintains a 16-slot array
mapping handle → slot index (0–15). Slot assignment is first-come, first-served.
The 4-bit slot index fits in one byte of the wire frame. The host reconstructs
task identity from `TASK_CREATED` and `TASK_NAME` events.

### Name Encoding

Task names are sent as one or more `TASK_NAME` frames. Each frame encodes 4 ASCII
bytes of the name in the 32-bit timestamp field (reusing that field's bytes since
a creation event has a real timestamp already). The `param` byte carries the
sequence number (0, 1, 2, …). The host decoder reassembles the chunks in sequence
order. Names are limited to 16 characters (4 frames).

### Tick Wrap-Around

The 32-bit tick counter wraps at 0xFFFFFFFF (49.7 days at 1000 Hz). The host
decoder detects the transition from a large value to a small one and increments a
32-bit high word, producing a 64-bit monotonic timestamp. This handles captures
that cross the wrap boundary.

## Host Layer

### Decoder (`core/decoder.py`)

The decoder is a stateful byte-stream consumer. Its internal buffer accumulates
raw bytes from `feed()` calls. On each call it:

1. Scans for the magic byte `0xA5` at the current buffer start (re-sync if noise)
2. Checks for a complete 8-byte frame
3. Unpacks the frame with `struct.unpack("<BB I BB")`
4. Extends the 32-bit tick to 64-bit
5. Constructs a frozen `TraceEvent` dataclass and appends it to the result list

The decoder is safe to call incrementally — feeding 4 bytes, then 4 more bytes,
produces the same result as feeding all 8 at once.

### Scheduling Model (`core/model.py`)

`build_model()` makes a single pass over the sorted event list and maintains the
following running state:

**Execution slices**: When `SWITCHED_IN` is observed, the current task and start
tick are recorded. When `SWITCHED_OUT` arrives, an `ExecSlice` is appended with
the duration and switch-out reason.

**Priority inversion detection**: A priority inversion begins when:
- A `MUTEX_BLOCKED` event arrives for task H
- The mutex is currently owned by task L
- H has higher priority than L (`task_prio[H] > task_prio[L]`)

The detector opens an `PriorityInversion` record keyed on the mutex ID. Any task
that runs a `SWITCHED_IN` during the open inversion window (and is neither H nor L)
is recorded in `inv.med_tasks`. The inversion closes when `MUTEX_GIVEN` is seen
for that mutex. Inversions still open at end-of-trace are closed at `last_ts`.

The `resolved_by_inheritance` flag is set if a `PRIO_INHERIT` event was observed
for the low task during the open inversion window — indicating FreeRTOS priority
inheritance resolved the unbounded blocking.

**Deadline misses**: `DEADLINE_MISS` events are collected directly into
`DeadlineMiss` records with the overrun in ticks.

### Gantt Renderer (`report/gantt.py`)

The renderer serializes the model into a JSON object and injects it into a
self-contained HTML template. The template contains all JavaScript inline — no
CDN dependencies, no external files.

The visualization is built on a `<canvas>` element with vanilla JavaScript:

- `ticksToX(t)` maps a tick value to a pixel x-coordinate given the current zoom
  level and pan offset
- `xToTick(x)` is the inverse, used for tooltip hit-testing
- Zoom is implemented by scaling `SCALE0 × zoom` and adjusting `panX` to keep
  the point under the mouse cursor stationary (pinch-zoom semantics)
- Pan is implemented by tracking mouse drag delta and updating `panX`
- Mouse-wheel and drag events are both handled
- `roundRect()` is used for execution slice corners (native in modern browsers)

The tick axis label density adapts to zoom level by choosing the smallest step from
`[10, 25, 50, 100, 200, 500, 1000, 2000, 5000]` ticks that keeps labels at least
40 pixels apart.

### CPU Utilization

`cpu_utilization()` divides each task's total slice duration by the total trace
duration. The result is bounded to `[0, 1]` per task and the sum across all tasks
is at most 1.0 (idle time is not accounted for as a task).

## Design Decisions

**8-byte fixed frame size**: Fixed frames allow the decoder to resync after any
number of corrupt bytes by scanning for the magic byte. Variable-length frames
would require a length field and make resync significantly harder. Eight bytes is
large enough for all needed fields without padding and small enough to fit hundreds
of events per millisecond even at 115200 baud.

**Ring buffer instead of DMA**: DMA would improve throughput but requires
platform-specific configuration (UART DMA channel, memory alignment, cache
coherency on Cortex-M7). The ring buffer approach keeps the firmware code
portable across any MCU with a byte-by-byte UART send function.

**Task slot IDs instead of handle addresses**: Raw TCB pointer addresses are 4
bytes, which would require a wider frame format. A 4-bit slot index fits in one
byte. The 16-slot limit is sufficient for all typical FreeRTOS applications — most
embedded systems run 4–12 tasks.

**No DBC or RTOS-internal symbols required**: The tool works on any FreeRTOS
application without needing debug symbols, a `.map` file, or a device-specific
configuration. All scheduling information is reconstructed from the event stream
alone.
