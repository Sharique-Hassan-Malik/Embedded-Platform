# RTOS Scheduler Visualizer

Instruments a FreeRTOS application with lightweight trace hooks, streams
scheduling events over UART, and renders an interactive Gantt chart showing
task execution, priority inversions and deadline misses in the browser.

Works fully offline via a built-in synthetic trace generator — no hardware required
to explore the tool.

## What it shows

- **Gantt timeline** — per-task execution slices color-coded by task, zoomable and
  pannable, with hover tooltips showing duration, priority and switch reason
- **Priority inversions** — highlighted as amber spans across the timeline with a
  tooltip identifying which high-priority task blocked on which low-priority task's
  mutex, and whether FreeRTOS priority inheritance resolved it
- **Deadline misses** — red vertical markers with overrun duration in ticks
- **CPU utilization** — per-task percentage and context-switch count in the
  statistics table
- **Rich terminal summary** — quick overview without opening a browser

## Quick start (no hardware)

```
pip install rich
pip install -e .
rtos-viz demo
```

Opens `demo.html` with a simulated three-task system showing a priority inversion
episode and a deadline miss.

```
rtos-viz demo --no-inversion --no-deadline-miss --output clean.html
```

## Decoding a real capture

Save the raw binary UART output from the target MCU to a file, then:

```
rtos-viz decode trace.bin
rtos-viz html   trace.bin --output gantt.html
```

## Live capture

Requires `pyserial` (`pip install pyserial`):

```
rtos-viz live /dev/ttyUSB0 --baud 921600 --duration 30 --output live.html
```

## CLI reference

```
rtos-viz decode FILE [--tps HZ]
rtos-viz html   FILE [--output FILE] [--tps HZ] [--title TEXT]
rtos-viz demo        [--output FILE] [--duration TICKS] [--no-inversion]
                     [--no-deadline-miss] [--tps HZ] [--seed N]
rtos-viz live   PORT [--baud N] [--output FILE] [--duration SEC] [--tps HZ]
```

`--tps` sets the FreeRTOS tick rate in Hz (default: 1000 = 1 ms per tick).

## Firmware integration

Copy `firmware/src/trace_hooks.c` and `firmware/include/trace_hooks.h` into
your FreeRTOS project, then add the macro definitions from
`firmware/include/FreeRTOSConfig_trace_snippet.h` to your `FreeRTOSConfig.h`.

Implement the one platform stub:

```c
void Trace_UART_Send(uint8_t byte)
{
    HAL_UART_Transmit(&huart2, &byte, 1, HAL_MAX_DELAY);
}
```

Call `Trace_Init()` before `vTaskStartScheduler()`.  Call `Trace_Flush()` from a
low-priority task or the idle hook to drain the ring buffer to UART.

See `firmware/demo/main.c` for a complete three-task demo with priority inversion
and deadline miss instrumentation on STM32.

### Recommended UART settings

| Parameter | Value |
|-----------|-------|
| Baud rate | 921600 |
| Data bits | 8 |
| Parity    | None |
| Stop bits | 1 |

At 921600 baud the 8-byte frame takes ~87 µs to transmit. A 1000 Hz tick rate
with typical context-switch activity stays well within the bandwidth budget.

## Wire protocol

Every event is an 8-byte frame:

```
Offset  Size  Field
0       1     Magic = 0xA5 (sync byte for re-synchronization)
1       1     Event type
2       4     Tick count (little-endian uint32, extended to 64-bit by host)
6       1     Task slot ID (0–15)
7       1     Parameter (priority, mutex ID, overrun ticks, etc.)
```

The magic byte allows the host decoder to re-sync after UART noise or buffer
overflow. Tick counter wrap-around is handled transparently by maintaining a
64-bit extended counter on the host side.

### Event types

| Code | Name | Param |
|------|------|-------|
| 0x01 | SWITCHED_IN | Current priority |
| 0x02 | SWITCHED_OUT | Reason (0=preempt 1=yield 2=block 3=delete) |
| 0x03 | TASK_CREATED | Initial priority |
| 0x04 | TASK_DELETED | — |
| 0x05 | TASK_READY | Priority |
| 0x06 | TASK_BLOCKED | Block reason |
| 0x07 | MUTEX_TAKEN | Mutex ID |
| 0x08 | MUTEX_GIVEN | Mutex ID |
| 0x09 | MUTEX_BLOCKED | Mutex ID |
| 0x0A | DEADLINE_MISS | Overrun ticks (saturated at 255) |
| 0x0B | TICK | — (heartbeat, emitted every 64 ticks) |
| 0x0C | PRIO_INHERIT | Inherited priority |
| 0x0D | PRIO_RESTORE | Restored priority |
| 0x0E | TASK_NAME | Name chunk (ts field holds 4 ASCII bytes) |

## Python API

```python
from scheduler_viz.core.decoder import Decoder
from scheduler_viz.core.model import build_model
from scheduler_viz.report.gantt import render_html
from scheduler_viz.report.terminal import render
from rich.console import Console

data   = open("trace.bin", "rb").read()
events = Decoder().feed(data)
model  = build_model(events, ticks_per_sec=1000)

render(model, Console())
open("gantt.html", "w").write(render_html(model))

for inv in model.inversions:
    print(f"Inversion on mutex {inv.mutex_id}: "
          f"Task{inv.high_task} blocked by Task{inv.low_task} "
          f"for {inv.duration} ticks")

for miss in model.misses:
    print(f"Deadline miss on Task{miss.task_id} at tick {miss.ts}, "
          f"overrun {miss.overrun_ticks} ticks")
```

## Running tests

```
pip install pytest pytest-cov
pytest host/tests/ -v
pytest host/tests/ --cov=scheduler_viz --cov-report=term-missing
```

## Project structure

```
firmware/
    include/
        trace_protocol.h            — 8-byte wire format definition
        trace_hooks.h               — hook function declarations
        FreeRTOSConfig_trace_snippet.h — FreeRTOS macro bindings
    src/
        trace_hooks.c               — ring-buffered hook implementations
    demo/
        main.c                      — 3-task demo (priority inversion + deadline miss)

host/
    scheduler_viz/
        __init__.py
        cli.py                      — decode / html / demo / live subcommands
        core/
            decoder.py              — binary frame decoder with resync and 64-bit ts
            model.py                — scheduling model (slices, inversions, misses)
            generator.py            — synthetic trace generator
        transport/
            serial_transport.py     — live UART capture via pyserial
        report/
            gantt.py                — self-contained interactive HTML Gantt
            terminal.py             — Rich terminal summary
    tests/
        conftest.py
        test_decoder.py
        test_model.py
        test_report.py
docs/
    architecture.md
```
