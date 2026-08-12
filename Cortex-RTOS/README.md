# CortexRTOS

A minimal preemptive RTOS for ARM Cortex-M4, written entirely from scratch. No HAL, no CMSIS, no vendor library. Runs on an STM32F401RE Nucleo board.

## Features

- Priority-based preemptive scheduler with round-robin within a priority level
- O(1) task election via 32-bit bitmask and `__builtin_ctz`
- Context switch in hand-written Thumb-2 assembly (PendSV + PSP)
- Binary mutex with priority inheritance
- Counting semaphore with ISR-safe signal
- Fixed-size memory pool (O(1) alloc and free)
- Task delay via SysTick tick counter
- Task suspend and resume
- Up to 8 priority levels and 8 tasks (configurable at compile time)
- Bare-register STM32F401 HAL: 84 MHz PLL, UART, LED, DWT delay
- Full host-side unit test suite — no cross-compiler needed

## Hardware

STM32F401RE Nucleo board (or any STM32F401 with an 8 MHz HSE crystal).

| Signal | Pin |
|---|---|
| Debug UART TX | PA2 (USART2, 115200 baud) |
| Debug UART RX | PA3 |
| Onboard LED | PA5 |

Connect a USB-UART adapter to PA2/PA3 to see the demo output.

## Project structure

```
cortex-rtos/
├── kernel/
│   ├── include/
│   │   ├── rtos.h           public API
│   │   ├── rtos_types.h     TCB, task states, return codes
│   │   ├── rtos_config.h    compile-time knobs
│   │   ├── rtos_objects.h   internal struct definitions
│   │   └── scheduler.h      internal scheduler API
│   └── src/
│       ├── kernel.c         init, tick, task create/delay/suspend
│       ├── scheduler.c      ready queues, bitmask election, round-robin
│       ├── mutex.c          binary mutex + priority inheritance
│       ├── semaphore.c      counting semaphore
│       └── pool.c           fixed-size memory pool
├── port/
│   └── cortex-m4/
│       ├── port_asm.s       PendSV_Handler, port_start_first_task, SysTick_Handler
│       └── port.c           initial stack frame layout
├── hal/
│   ├── include/hal.h
│   └── src/hal.c            STM32F401 bare-register peripheral drivers
├── app/
│   └── main.c               demo: LED, producer, consumer, stats tasks
├── tests/
│   └── host/
│       ├── test_kernel.c    31 unit tests (5 groups)
│       ├── host_stubs.c     port stubs for native compilation
│       └── Makefile
├── tools/
│   ├── stm32f401re.ld       linker script
│   └── startup_stm32f401.s  vector table and Reset_Handler
├── docs/
│   └── architecture.md
└── Makefile
```

## Building the firmware

Install `arm-none-eabi-gcc` (any version supporting Cortex-M4 hard-float).

```bash
make          # produces build/cortex-rtos.elf and build/cortex-rtos.bin
make size     # show .text / .data / .bss sizes
make flash    # program via OpenOCD (requires st-link)
make clean
```

## Running the host unit tests

No cross-compiler needed — only standard `gcc`.

```bash
make test
```

Output:

```
CortexRTOS host unit tests
==========================

T1  Scheduler
  task_create returns non-NULL                        PASS
  elect returns highest priority task                 PASS
  ...

==========================
Results: 31 passed, 0 failed
```

## Configuration

Edit `kernel/include/rtos_config.h` or pass defines on the compiler command line:

| Macro | Default | Description |
|---|---|---|
| `RTOS_MAX_TASKS` | 8 | Maximum tasks including idle |
| `RTOS_TICK_HZ` | 1000 | SysTick frequency in Hz |
| `RTOS_CORE_CLOCK_HZ` | 84000000 | Core clock (used for SysTick reload) |
| `RTOS_DEFAULT_STACK_BYTES` | 512 | Default task stack size |
| `RTOS_HEAP_BYTES` | 4096 | Internal heap for default stacks |
| `RTOS_PRIORITY_LEVELS` | 8 | Number of priority levels (max 32) |

## Demo application output

```
CortexRTOS booting...
Tasks created. Starting scheduler.
[   300] producer: sent msg #0
[   300] consumer: got "msg #0"
[   500] led task toggles LED
[   600] producer: sent msg #1
[   600] consumer: got "msg #1"
[  2000] stats: tasks=5 tick=2000 pool_free=4
  idle         prio=7 state=0 runs=1820
  led          prio=2 state=2 runs=4
  producer     prio=1 state=2 runs=6
  consumer     prio=1 state=2 runs=6
  stats        prio=3 state=2 runs=1
```

## Key design decisions

**No dynamic allocation after init.** The task table and all kernel objects are statically sized. The internal heap is a one-shot bump allocator used only for default task stacks; once `rtos_start()` is called, no further allocation occurs.

**PendSV for context switches.** Setting PENDSVSET in SCB->ICSR queues a switch at the lowest exception priority. Any ISR can trigger a switch safely — the actual switch happens after all ISRs exit.

**Priority inheritance on mutex.** Without it, a high-priority task blocked on a mutex held by a low-priority task can be starved indefinitely by medium-priority tasks. Inheritance raises the holder's effective priority to the waiter's level until the mutex is released.

**`__builtin_ctz` for O(1) election.** A 32-bit bitmask records which priority levels are non-empty. `ctz` (count trailing zeros) finds the lowest set bit in one instruction pair on Cortex-M4 (RBIT + CLZ). No loop over all priority levels is needed.
