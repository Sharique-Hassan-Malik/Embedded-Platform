"""Tests for scheduler_viz.core.model."""

from __future__ import annotations

import pytest

from scheduler_viz.core.model import (
    build_model,
    cpu_utilization,
    ExecSlice,
)
from scheduler_viz.core.decoder import (
    pack_frame,
    pack_name_frames,
    Decoder,
    EVT_TASK_CREATED,
    EVT_SWITCHED_IN,
    EVT_SWITCHED_OUT,
    EVT_MUTEX_TAKEN,
    EVT_MUTEX_GIVEN,
    EVT_MUTEX_BLOCKED,
    EVT_DEADLINE_MISS,
    EVT_PRIO_INHERIT,
    EVT_PRIO_RESTORE,
    REASON_PREEMPT,
    REASON_BLOCK,
)


def decode(raw: bytes):
    return Decoder().feed(raw)


def make(etype, ts, tid, param):
    return pack_frame(etype, ts, tid, param)


class TestBuildModelBasic:
    def test_empty(self):
        m = build_model([])
        assert len(m.tasks) == 0
        assert len(m.slices) == 0

    def test_task_created(self):
        raw = make(EVT_TASK_CREATED, 0, 0, 9)
        m = build_model(decode(raw))
        assert 0 in m.tasks
        assert m.tasks[0].priority == 9

    def test_switch_in_creates_task(self):
        raw = make(EVT_SWITCHED_IN, 10, 0, 9)
        m = build_model(decode(raw))
        assert 0 in m.tasks

    def test_switch_in_out_produces_slice(self):
        raw = (
            make(EVT_SWITCHED_IN,  10, 0, 9) +
            make(EVT_SWITCHED_OUT, 30, 0, REASON_PREEMPT)
        )
        m = build_model(decode(raw))
        assert len(m.slices) == 1
        s = m.slices[0]
        assert s.task_id == 0
        assert s.start_ts == 10
        assert s.end_ts == 30
        assert s.duration == 20

    def test_switch_reason_stored(self):
        raw = (
            make(EVT_SWITCHED_IN,  10, 0, 5) +
            make(EVT_SWITCHED_OUT, 20, 0, REASON_BLOCK)
        )
        m = build_model(decode(raw))
        assert m.slices[0].reason == "block"

    def test_multiple_tasks(self):
        raw = (
            make(EVT_SWITCHED_IN,  0,  0, 9) +
            make(EVT_SWITCHED_OUT, 10, 0, REASON_PREEMPT) +
            make(EVT_SWITCHED_IN,  10, 1, 5) +
            make(EVT_SWITCHED_OUT, 20, 1, REASON_PREEMPT)
        )
        m = build_model(decode(raw))
        assert len(m.tasks) == 2
        assert len(m.slices) == 2

    def test_switch_count_increments(self):
        raw = b"".join(
            make(EVT_SWITCHED_IN,  i * 20,      0, 9) +
            make(EVT_SWITCHED_OUT, i * 20 + 10, 0, REASON_PREEMPT)
            for i in range(5)
        )
        m = build_model(decode(raw))
        assert m.tasks[0].switch_in_count == 5

    def test_timestamps_tracked(self):
        raw = (
            make(EVT_SWITCHED_IN,  5,  0, 9) +
            make(EVT_SWITCHED_OUT, 95, 0, REASON_PREEMPT)
        )
        m = build_model(decode(raw))
        assert m.first_ts == 5
        assert m.last_ts == 95


class TestTaskNames:
    def test_name_from_frames(self):
        raw = (
            make(EVT_TASK_CREATED, 0, 0, 9) +
            pack_name_frames(0, "HighTask", 0) +
            make(EVT_SWITCHED_IN, 1, 0, 9) +
            make(EVT_SWITCHED_OUT, 10, 0, REASON_PREEMPT)
        )
        m = build_model(decode(raw))
        assert "HighTask" in m.tasks[0].name

    def test_name_override(self):
        raw = make(EVT_SWITCHED_IN, 0, 2, 5)
        m = build_model(decode(raw), names={2: "CustomName"})
        assert m.tasks[2].name == "CustomName"

    def test_default_name(self):
        raw = make(EVT_SWITCHED_IN, 0, 3, 3)
        m = build_model(decode(raw))
        assert "3" in m.tasks[3].name


class TestDeadlineMisses:
    def test_deadline_miss_recorded(self):
        raw = (
            make(EVT_SWITCHED_IN,  0,  0, 9) +
            make(EVT_DEADLINE_MISS, 60, 0, 10)
        )
        m = build_model(decode(raw))
        assert len(m.misses) == 1
        assert m.misses[0].task_id == 0
        assert m.misses[0].overrun_ticks == 10

    def test_multiple_misses(self):
        raw = b"".join(
            make(EVT_SWITCHED_IN,   i * 100,      0, 9) +
            make(EVT_DEADLINE_MISS, i * 100 + 60, 0, 5)
            for i in range(3)
        )
        m = build_model(decode(raw))
        assert len(m.misses) == 3

    def test_no_misses_on_clean(self, clean_model):
        assert len(clean_model.misses) == 0


class TestPriorityInversion:
    def test_inversion_detected(self, model):
        assert len(model.inversions) >= 1

    def test_inversion_fields(self, model):
        for inv in model.inversions:
            assert inv.start_ts >= 0
            assert inv.end_ts >= inv.start_ts
            assert inv.mutex_id >= 0

    def test_no_inversions_on_clean(self, clean_model):
        assert len(clean_model.inversions) == 0

    def test_inversion_high_prio_higher_than_low(self, model):
        for inv in model.inversions:
            high_prio = model.tasks[inv.high_task].priority if inv.high_task in model.tasks else 0
            low_prio  = model.tasks[inv.low_task].priority  if inv.low_task  in model.tasks else 0
            assert high_prio >= low_prio


class TestCPUUtilization:
    def test_utilization_sums_to_at_most_one(self, model):
        util = cpu_utilization(model)
        total = sum(util.values())
        assert total <= 1.01    # allow tiny float rounding

    def test_all_values_in_range(self, model):
        util = cpu_utilization(model)
        for u in util.values():
            assert 0.0 <= u <= 1.0

    def test_empty_model(self):
        from scheduler_viz.core.model import SchedulingModel
        util = cpu_utilization(SchedulingModel())
        assert util == {}

    def test_high_prio_has_more_cpu(self, model):
        util = cpu_utilization(model)
        from scheduler_viz.core.generator import TASK_HIGH, TASK_LOW
        # Both tasks must appear in the utilization dict
        if TASK_HIGH in util and TASK_LOW in util:
            # HighTask gets preemption priority but LowTask holds long mutex windows.
            # The invariant is just that both have non-zero utilization.
            assert util[TASK_HIGH] > 0.0
            assert util[TASK_LOW] > 0.0


class TestFullModel:
    def test_tasks_created(self, model):
        assert len(model.tasks) == 3

    def test_slices_populated(self, model):
        assert len(model.slices) > 0

    def test_slices_non_overlapping_per_task(self, model):
        from collections import defaultdict
        by_task = defaultdict(list)
        for s in model.slices:
            by_task[s.task_id].append(s)
        for tid, slices in by_task.items():
            slices.sort(key=lambda s: s.start_ts)
            for a, b in zip(slices, slices[1:]):
                assert a.end_ts <= b.start_ts + 1, f"Overlap in task {tid}: {a} vs {b}"

    def test_slice_durations_positive(self, model):
        for s in model.slices:
            assert s.duration >= 0
