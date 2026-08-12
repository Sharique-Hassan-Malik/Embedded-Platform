"""
Scheduling model.

Reconstructs the full scheduling history from a decoded event stream:

  - Per-task execution slices (start tick, end tick, priority at time)
  - Priority inversion instances with causal chain
  - Deadline miss records
  - Context-switch counts and CPU utilization per task
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

from scheduler_viz.core.decoder import (
    TraceEvent,
    EVT_SWITCHED_IN,
    EVT_SWITCHED_OUT,
    EVT_TASK_CREATED,
    EVT_TASK_DELETED,
    EVT_TASK_READY,
    EVT_TASK_BLOCKED,
    EVT_MUTEX_TAKEN,
    EVT_MUTEX_GIVEN,
    EVT_MUTEX_BLOCKED,
    EVT_DEADLINE_MISS,
    EVT_PRIO_INHERIT,
    EVT_PRIO_RESTORE,
    EVT_TASK_NAME,
    EVT_TICK,
    REASON_NAMES,
)


@dataclass
class ExecSlice:
    """One contiguous period of CPU execution for a task."""
    task_id:   int
    start_ts:  int      # inclusive (ticks)
    end_ts:    int      # exclusive (ticks)
    priority:  int
    reason:    str = "preempt"    # why it ended

    @property
    def duration(self) -> int:
        return max(0, self.end_ts - self.start_ts)


@dataclass
class PriorityInversion:
    """
    A priority inversion episode.

    low_task holds a mutex that high_task is waiting for.  med_task preempts
    low_task during this window, effectively delaying high_task.

    start_ts  : tick when high_task first blocked on the mutex
    end_ts    : tick when low_task released the mutex
    """
    mutex_id:   int
    high_task:  int
    low_task:   int
    med_tasks:  List[int]    # tasks that ran during the inversion
    start_ts:   int
    end_ts:     int = 0
    resolved_by_inheritance: bool = False

    @property
    def duration(self) -> int:
        return max(0, self.end_ts - self.start_ts)


@dataclass
class DeadlineMiss:
    task_id:     int
    ts:          int
    overrun_ticks: int


@dataclass
class TaskInfo:
    task_id:   int
    name:      str = ""
    priority:  int = 0
    created_ts: int = 0
    deleted_ts: int = -1
    switch_in_count: int = 0


@dataclass
class SchedulingModel:
    tasks:       Dict[int, TaskInfo]          = field(default_factory=dict)
    slices:      List[ExecSlice]              = field(default_factory=list)
    inversions:  List[PriorityInversion]      = field(default_factory=list)
    misses:      List[DeadlineMiss]           = field(default_factory=list)
    tick_count:  int                          = 0
    first_ts:    int                          = 0
    last_ts:     int                          = 0


def build_model(
    events: List[TraceEvent],
    names: Optional[Dict[int, str]] = None,
    ticks_per_sec: int = 1000,
) -> SchedulingModel:
    """
    Reconstruct the scheduling model from a decoded event list.

    Parameters
    ----------
    events       : sorted by ts (ascending)
    names        : optional override map task_id → name (e.g. from TASK_NAME frames)
    ticks_per_sec: FreeRTOS tick rate for duration labeling
    """
    model = SchedulingModel()
    if not events:
        return model

    model.first_ts = events[0].ts
    model.last_ts  = events[-1].ts

    # Running state
    running_task: Optional[int] = None
    running_since: int = 0
    running_prio: int = 0

    task_prio: Dict[int, int] = {}

    # Mutex ownership: mutex_id → task_id
    mutex_owner: Dict[int, int] = {}
    # Mutex waiters: mutex_id → [task_id]
    mutex_waiters: Dict[int, List[int]] = {}

    # Open inversion episodes: mutex_id → PriorityInversion
    open_inversions: Dict[int, PriorityInversion] = {}

    # Collect task names from TASK_NAME events first
    name_chunks: Dict[int, Dict[int, bytes]] = {}
    import struct
    for e in events:
        if e.type == EVT_TASK_NAME:
            if e.task_id not in name_chunks:
                name_chunks[e.task_id] = {}
            raw = struct.pack("<I", e.raw_ts)
            name_chunks[e.task_id][e.param] = raw

    assembled_names: Dict[int, str] = {}
    for tid, chunks in name_chunks.items():
        data = b""
        for seq in sorted(chunks):
            data += chunks[seq]
        assembled_names[tid] = data.rstrip(b"\x00").decode("ascii", errors="replace")

    if names:
        assembled_names.update(names)

    def ensure_task(tid: int) -> TaskInfo:
        if tid not in model.tasks:
            model.tasks[tid] = TaskInfo(
                task_id=tid,
                name=assembled_names.get(tid, f"Task{tid}"),
            )
        return model.tasks[tid]

    for e in events:
        model.last_ts = e.ts

        if e.type == EVT_TASK_CREATED:
            info = ensure_task(e.task_id)
            info.created_ts = e.ts
            info.priority = e.param
            task_prio[e.task_id] = e.param

        elif e.type == EVT_TASK_DELETED:
            ensure_task(e.task_id).deleted_ts = e.ts
            if running_task == e.task_id:
                model.slices.append(ExecSlice(
                    task_id=e.task_id,
                    start_ts=running_since,
                    end_ts=e.ts,
                    priority=running_prio,
                    reason="delete",
                ))
                running_task = None

        elif e.type == EVT_SWITCHED_IN:
            running_task = e.task_id
            running_since = e.ts
            running_prio = e.param
            task_prio[e.task_id] = e.param
            info = ensure_task(e.task_id)
            info.priority = e.param
            info.switch_in_count += 1

        elif e.type == EVT_SWITCHED_OUT:
            if running_task is not None:
                reason = REASON_NAMES.get(e.param, "preempt")
                model.slices.append(ExecSlice(
                    task_id=running_task,
                    start_ts=running_since,
                    end_ts=e.ts,
                    priority=running_prio,
                    reason=reason,
                ))
                running_task = None

        elif e.type == EVT_MUTEX_TAKEN:
            mutex_owner[e.param] = e.task_id

        elif e.type == EVT_MUTEX_GIVEN:
            mid = e.param
            giver = e.task_id
            mutex_owner.pop(mid, None)
            if mid in open_inversions:
                inv = open_inversions.pop(mid)
                inv.end_ts = e.ts
                model.inversions.append(inv)

        elif e.type == EVT_MUTEX_BLOCKED:
            mid = e.param
            waiter = e.task_id
            mutex_waiters.setdefault(mid, []).append(waiter)
            owner = mutex_owner.get(mid)
            if owner is not None:
                waiter_prio = task_prio.get(waiter, 0)
                owner_prio  = task_prio.get(owner, 0)
                if waiter_prio > owner_prio and mid not in open_inversions:
                    open_inversions[mid] = PriorityInversion(
                        mutex_id=mid,
                        high_task=waiter,
                        low_task=owner,
                        med_tasks=[],
                        start_ts=e.ts,
                    )

        elif e.type == EVT_PRIO_INHERIT:
            if running_task is not None:
                task_prio[running_task] = e.param
                for inv in open_inversions.values():
                    if inv.low_task == running_task:
                        inv.resolved_by_inheritance = True

        elif e.type == EVT_PRIO_RESTORE:
            if running_task is not None:
                task_prio[running_task] = e.param

        elif e.type == EVT_DEADLINE_MISS:
            ensure_task(e.task_id)
            model.misses.append(DeadlineMiss(
                task_id=e.task_id,
                ts=e.ts,
                overrun_ticks=e.param,
            ))

        elif e.type == EVT_TICK:
            model.tick_count += 1

        # Track which tasks ran during open inversions
        if e.type == EVT_SWITCHED_IN:
            for inv in open_inversions.values():
                if (e.task_id != inv.high_task
                        and e.task_id != inv.low_task
                        and e.task_id not in inv.med_tasks):
                    inv.med_tasks.append(e.task_id)

    # Close any inversions still open at end of trace
    for inv in open_inversions.values():
        inv.end_ts = model.last_ts
        model.inversions.append(inv)

    # Ensure all task names are set
    for tid in model.tasks:
        if not model.tasks[tid].name:
            model.tasks[tid].name = assembled_names.get(tid, f"Task{tid}")

    return model


def cpu_utilization(model: SchedulingModel) -> Dict[int, float]:
    """Return fraction of total trace duration each task was running (0.0–1.0)."""
    total = max(1, model.last_ts - model.first_ts)
    util: Dict[int, float] = {}
    for s in model.slices:
        util[s.task_id] = util.get(s.task_id, 0.0) + s.duration / total
    return util
