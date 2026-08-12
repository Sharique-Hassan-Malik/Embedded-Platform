"""
Synthetic trace generator.

Simulates a FreeRTOS scheduler with three tasks, a shared mutex and
intentional priority inversions and deadline misses.  Produces raw bytes
in the wire format so the decoder can be tested end-to-end.
"""

from __future__ import annotations

import random
from typing import List, Optional

from scheduler_viz.core.decoder import (
    pack_frame,
    pack_name_frames,
    EVT_SWITCHED_IN,
    EVT_SWITCHED_OUT,
    EVT_TASK_CREATED,
    EVT_MUTEX_TAKEN,
    EVT_MUTEX_GIVEN,
    EVT_MUTEX_BLOCKED,
    EVT_DEADLINE_MISS,
    EVT_PRIO_INHERIT,
    EVT_PRIO_RESTORE,
    EVT_TICK,
    REASON_PREEMPT,
    REASON_BLOCK,
)

# Task slot assignments
TASK_HIGH = 0
TASK_MED  = 1
TASK_LOW  = 2

# Default parameters
HIGH_PERIOD  = 100
MED_PERIOD   = 70
LOW_PERIOD   = 150
HIGH_WORK    = 15
MED_WORK     = 25
LOW_WORK     = 60
DEADLINE     = 50
MUTEX_ID     = 0


def generate(
    duration_ticks: int = 2000,
    seed: Optional[int] = 42,
    inversion: bool = True,
    deadline_miss: bool = True,
) -> bytes:
    """
    Generate a synthetic FreeRTOS trace as raw bytes.

    Parameters
    ----------
    duration_ticks : total simulation length in ticks
    seed           : RNG seed for reproducibility
    inversion      : inject a priority inversion episode
    deadline_miss  : inject a deadline miss
    """
    rng = random.Random(seed)
    buf = bytearray()

    def emit(etype: int, ts: int, tid: int, param: int) -> None:
        buf.extend(pack_frame(etype, ts, tid, param))

    # Task creation at t=0
    emit(EVT_TASK_CREATED, 0, TASK_HIGH, 9)   # prio 9
    emit(EVT_TASK_CREATED, 0, TASK_MED,  5)   # prio 5
    emit(EVT_TASK_CREATED, 0, TASK_LOW,  2)   # prio 2
    buf.extend(pack_name_frames(TASK_HIGH, "HighTask", 0))
    buf.extend(pack_name_frames(TASK_MED,  "MedTask",  0))
    buf.extend(pack_name_frames(TASK_LOW,  "LowTask",  0))

    # State
    next_high = HIGH_PERIOD
    next_med  = MED_PERIOD
    next_low  = LOW_PERIOD

    inversion_done     = False
    deadline_miss_done = False
    mutex_held         = False
    current_task       = TASK_HIGH
    slice_start        = 1

    emit(EVT_SWITCHED_IN, 1, TASK_HIGH, 9)

    tick = 2
    while tick < duration_ticks:
        jitter = rng.randint(-2, 2)

        # ── HighTask period ──
        if tick >= next_high:
            work = HIGH_WORK + jitter

            # Deadline miss: once, add extra work
            if deadline_miss and not deadline_miss_done and tick > duration_ticks // 2:
                work += DEADLINE + 15
                deadline_miss_done = True

            if current_task != TASK_HIGH:
                emit(EVT_SWITCHED_OUT, tick, current_task, REASON_PREEMPT)
                emit(EVT_SWITCHED_IN,  tick, TASK_HIGH, 9)
                current_task = TASK_HIGH
                slice_start  = tick

            # If LowTask holds mutex, block (priority inversion scenario)
            if inversion and not inversion_done and mutex_held and tick > duration_ticks // 3:
                emit(EVT_MUTEX_BLOCKED, tick, TASK_HIGH, MUTEX_ID)
                emit(EVT_SWITCHED_OUT,  tick, TASK_HIGH, REASON_BLOCK)
                # LowTask inherits priority
                emit(EVT_PRIO_INHERIT, tick, TASK_LOW, 9)
                # MedTask runs while LowTask has inherited priority
                emit(EVT_SWITCHED_IN, tick + 5, TASK_MED, 5)
                emit(EVT_SWITCHED_OUT, tick + 30, TASK_MED, REASON_PREEMPT)
                # LowTask finishes its critical section
                emit(EVT_SWITCHED_IN, tick + 30, TASK_LOW, 9)
                emit(EVT_MUTEX_GIVEN, tick + 55, TASK_LOW, MUTEX_ID)
                emit(EVT_PRIO_RESTORE, tick + 55, TASK_LOW, 2)
                emit(EVT_SWITCHED_OUT, tick + 55, TASK_LOW, REASON_PREEMPT)
                mutex_held = False
                # HighTask resumes
                emit(EVT_MUTEX_TAKEN,  tick + 55, TASK_HIGH, MUTEX_ID)
                emit(EVT_SWITCHED_IN,  tick + 55, TASK_HIGH, 9)
                current_task = TASK_HIGH
                slice_start  = tick + 55
                inversion_done = True
                tick = tick + 55 + work
            else:
                end = tick + work
                if work > DEADLINE:
                    overrun = min(255, work - DEADLINE)
                    emit(EVT_DEADLINE_MISS, end, TASK_HIGH, overrun)
                emit(EVT_SWITCHED_OUT, end, TASK_HIGH, REASON_PREEMPT)
                emit(EVT_SWITCHED_IN,  end, TASK_MED, 5)
                current_task = TASK_MED
                slice_start  = end
                tick = end

            next_high += HIGH_PERIOD
            continue

        # ── MedTask period ──
        if tick >= next_med:
            work = MED_WORK + jitter
            if current_task != TASK_MED:
                emit(EVT_SWITCHED_OUT, tick, current_task, REASON_PREEMPT)
                emit(EVT_SWITCHED_IN,  tick, TASK_MED, 5)
                current_task = TASK_MED
                slice_start  = tick
            end = tick + work
            emit(EVT_SWITCHED_OUT, end, TASK_MED, REASON_PREEMPT)
            emit(EVT_SWITCHED_IN,  end, TASK_LOW, 2)
            current_task = TASK_LOW
            slice_start  = end
            tick = end
            next_med += MED_PERIOD
            continue

        # ── LowTask period ──
        if tick >= next_low:
            work = LOW_WORK + jitter
            if current_task != TASK_LOW:
                emit(EVT_SWITCHED_OUT, tick, current_task, REASON_PREEMPT)
                emit(EVT_SWITCHED_IN,  tick, TASK_LOW, 2)
                current_task = TASK_LOW
                slice_start  = tick
            if inversion and not mutex_held and not inversion_done:
                # Take the mutex and hold it — do NOT give it back here.
                # HighTask will block on it in its next period and trigger inversion.
                emit(EVT_MUTEX_TAKEN, tick + 2, TASK_LOW, MUTEX_ID)
                mutex_held = True
            elif not inversion and mutex_held:
                # Non-inversion path: take and release normally
                emit(EVT_MUTEX_GIVEN, tick + work - 2, TASK_LOW, MUTEX_ID)
                mutex_held = False
            end = tick + work
            emit(EVT_SWITCHED_OUT, end, TASK_LOW, REASON_PREEMPT)
            emit(EVT_SWITCHED_IN,  end, TASK_HIGH, 9)
            current_task = TASK_HIGH
            slice_start  = end
            tick = end
            next_low += LOW_PERIOD
            continue

        # Heartbeat tick
        emit(EVT_TICK, tick, 0, 0)
        tick += 10

    # Final switch-out
    emit(EVT_SWITCHED_OUT, duration_ticks, current_task, REASON_PREEMPT)

    return bytes(buf)
