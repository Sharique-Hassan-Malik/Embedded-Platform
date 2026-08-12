# Architecture

## Overview

```
app/main.c              demo application — 4 tasks
      │
      ▼
kernel/include/rtos.h   public API (task, mutex, semaphore, pool)
      │
      ├── kernel/src/kernel.c      init, tick, task create, delay, suspend
      ├── kernel/src/scheduler.c   ready queues, election, bitmask
      ├── kernel/src/mutex.c       binary mutex + priority inheritance
      ├── kernel/src/semaphore.c   counting semaphore
      └── kernel/src/pool.c        fixed-size memory pool
              │
              ▼
      port/cortex-m4/
          port_asm.s    PendSV_Handler, port_start_first_task, SysTick_Handler
          port.c        port_stack_init — initial stack frame layout
              │
              ▼
      hal/src/hal.c     STM32F401 bare-register: clock, UART, LED, DWT
```

---

## Scheduler (`kernel/src/scheduler.c`)

### Ready queues

One circular singly-linked list per priority level, indexed by `_ready_tail[prio]`. Storing the tail pointer gives O(1) enqueue (at tail) and O(1) peek/dequeue (at head = `tail->next`) without a separate head pointer.

```
Priority 0:  [A] → [B] → [A]  (tail = B, head = A)
Priority 1:  [C] → [C]         (tail = C, single element)
Priority 2:  empty
```

### Priority bitmask

`_ready_mask` is a 32-bit integer where bit *N* is set when `_ready_tail[N] != NULL`. This limits the RTOS to 32 priority levels but makes `sched_elect` O(1):

```c
uint8_t prio = __builtin_ctz(_ready_mask);
```

`__builtin_ctz` (count trailing zeros) returns the index of the lowest set bit, which is the highest-priority non-empty level. On Cortex-M4 this compiles to `RBIT` + `CLZ` — two instructions.

### Round-robin

`sched_elect` rotates the tail pointer on each call without removing the task from the queue. The elected task (`head = tail->next`) becomes the new tail. Tasks at the same priority alternate on successive elections.

---

## Context switch (`port/cortex-m4/port_asm.s`)

The Cortex-M hardware automatically saves `{R0–R3, R12, LR, PC, xPSR}` on the PSP when taking an exception. PendSV_Handler saves and restores the remaining callee-saved registers `{R4–R11}` manually.

```
High addr
  xPSR        ┐
  PC          │  hardware-saved exception frame
  LR          │  (pushed/popped automatically by CPU)
  R12         │
  R3..R0      ┘
  R11         ┐
  R10         │  software-saved callee registers
  R9          │  (pushed by PendSV_Handler, stored in TCB->sp)
  R8          │
  R7          │
  R6          │
  R5          │
  R4          ┘  ← TCB->sp points here after save
Low addr
```

`TCB->sp` is always the first field (offset 0) so the assembly can load and store it with `ldr rN, [tcb, #0]` without knowing the full struct layout.

### PendSV priority

PendSV is configured at the lowest exception priority (0xFF). This means it only fires after all higher-priority ISRs complete, so a context switch never interrupts an ISR. Any ISR can safely call `sched_yield_needed()` and the switch happens on ISR exit.

### First task startup (`port_start_first_task`)

Sets PendSV priority, programs SysTick, loads the first task's SP from `_next_task->sp`, pops `{R4–R11}`, writes PSP, switches CONTROL to use PSP, then executes `bx lr`. The CPU pops the hardware frame from the newly installed PSP and enters the task function with `R0 = arg`.

---

## Initial stack frame (`port/cortex-m4/port.c`)

`port_stack_init` builds a 16-word frame at the top of the task's stack buffer so the first context restore works correctly:

| Offset from initial SP | Content |
|---|---|
| +15 (top) | xPSR = 0x01000000 (Thumb bit) |
| +14 | PC = task function address |
| +13 | LR = `_task_exit_hook` |
| +12 | R12 = 0 |
| +11..+8 | R3..R0; R0 = arg |
| +7..+0 | R11..R4 = 0 |

The initial SP stored in `TCB->sp` points to R4 (offset +0).

---

## Mutex (`kernel/src/mutex.c`)

Binary mutex with priority inheritance. The wait list is a singly-linked list ordered by priority (lowest index = highest priority first).

### Priority inheritance

When task H (high priority) blocks on a mutex held by task L (low priority), L's priority is raised to H's level. This prevents medium-priority tasks from preempting L and extending H's wait indefinitely.

```
Without inheritance:   H blocks → M preempts L → L never runs → H starves
With inheritance:      H blocks → L elevated → L completes → H woken
```

On unlock the owner's priority is restored to `base_priority` before the next waiter is woken.

---

## Semaphore (`kernel/src/semaphore.c`)

Counting semaphore with a configurable maximum count. The wait list is priority-ordered, identical to the mutex wait list.

`rtos_sem_signal_isr` is ISR-safe: it only marks a waiter ready and calls `sched_yield_needed()` (which sets PENDSVSET). It does not context-switch inline. The actual switch happens when the ISR exits and PendSV fires.

---

## Memory pool (`kernel/src/pool.c`)

Fixed-size block allocator over a caller-supplied buffer. Free blocks are threaded into a singly-linked list embedded inside the blocks themselves — the first `sizeof(void*)` bytes of each free block store the next-free pointer.

```
buf:  [next→B][data...] [next→C][data...] [next→NULL][data...]
       block A            block B            block C
free_head → A
```

`alloc`: pops head from free list. O(1).
`free`:  pushes block onto head of free list. O(1).

Block size is rounded up to `sizeof(void*)` alignment so the embedded pointer always fits.

---

## Kernel tick (`kernel/src/kernel.c`)

`rtos_tick()` is called from `SysTick_Handler` every `1/RTOS_TICK_HZ` seconds. It:

1. Increments `_tick_count`.
2. Scans `_delay_list` and moves tasks whose `wake_tick ≤ _tick_count` back into the ready queue.
3. If any woken task has higher priority than the current task, or another task at the same priority is ready (round-robin rotation), sets `_next_task` and calls `sched_yield_needed()`.

The delay list is unsorted and scanned linearly each tick. For `RTOS_MAX_TASKS ≤ 32` this costs at most 32 comparisons per tick, which is negligible at 1 kHz.

---

## HAL (`hal/src/hal.c`)

Bare-register access to STM32F401RE peripherals. No CMSIS, no ST HAL.

| Peripheral | Purpose |
|---|---|
| RCC + Flash | 84 MHz from HSE (8 MHz) via PLL: M=8, N=336, P=4 |
| GPIOA PA5 | Onboard LED (Nucleo green LED) |
| USART2 PA2/PA3 | Debug UART at configurable baud rate |
| DWT CYCCNT | Microsecond busy-loop delay |

---

## Build

### Firmware (STM32F401RE Nucleo)

```bash
make              # produces build/cortex-rtos.elf and .bin
make flash        # programs via OpenOCD + ST-Link
make size         # section sizes
```

Requires `arm-none-eabi-gcc` and OpenOCD.

### Host unit tests (no hardware)

```bash
make test         # builds and runs tests/host/test_kernel
```

Requires only `gcc`. All kernel C sources compile cleanly with `-UCORTEX_M`; port and hardware functions are replaced by stubs in `tests/host/host_stubs.c`.

---

## Test coverage

| Group | What is tested |
|---|---|
| T1 Scheduler | Priority ordering, highest-priority election, round-robin rotation within a level, `sched_unready` removal, bitmask clear when level empties, `RTOS_MAX_TASKS` limit |
| T2 Delay list | Task not elected before deadline, task elected at deadline, list empty after wakeup |
| T3 Memory pool | Initial free count, alloc until exhaustion, `NULL` on empty, free restores count, bounds check, `free(NULL)` safety, full cycle |
| T4 Mutex | First lock succeeds, owner set, `RTOS_NO_WAIT` timeout, unlock clears owner, waiter woken on unlock |
| T5 Semaphore | Initial count, decrement on wait, `RTOS_NO_WAIT` timeout, increment on signal, max_count clamping, waiter woken instead of increment, ISR-safe signal |
