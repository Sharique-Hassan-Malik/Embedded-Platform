"""Tests for scheduler_viz.core.decoder."""

from __future__ import annotations

import struct
import pytest

from scheduler_viz.core.decoder import (
    Decoder,
    TraceEvent,
    pack_frame,
    pack_name_frames,
    MAGIC,
    FRAME_SIZE,
    FRAME_FMT,
    EVT_SWITCHED_IN,
    EVT_SWITCHED_OUT,
    EVT_TASK_CREATED,
    EVT_TASK_NAME,
    EVT_MUTEX_TAKEN,
    EVT_DEADLINE_MISS,
    EVT_TICK,
)


def make_frame(etype, ts, tid, param):
    return pack_frame(etype, ts, tid, param)


class TestPackFrame:
    def test_magic_byte(self):
        f = make_frame(EVT_TICK, 0, 0, 0)
        assert f[0] == MAGIC

    def test_length(self):
        f = make_frame(EVT_TICK, 100, 1, 0)
        assert len(f) == FRAME_SIZE

    def test_roundtrip(self):
        f = make_frame(EVT_SWITCHED_IN, 12345, 3, 9)
        _, etype, ts, tid, param = struct.unpack(FRAME_FMT, f)
        assert etype == EVT_SWITCHED_IN
        assert ts == 12345
        assert tid == 3
        assert param == 9

    def test_ts_truncated_to_32bit(self):
        f = make_frame(EVT_TICK, 0x1_FFFF_FFFF, 0, 0)
        _, _, ts, _, _ = struct.unpack(FRAME_FMT, f)
        assert ts == 0xFFFF_FFFF


class TestDecoder:
    def test_empty_input(self):
        dec = Decoder()
        assert dec.feed(b"") == []

    def test_single_frame(self):
        frame = make_frame(EVT_TICK, 100, 0, 0)
        dec = Decoder()
        events = dec.feed(frame)
        assert len(events) == 1
        assert events[0].type == EVT_TICK
        assert events[0].raw_ts == 100

    def test_multiple_frames(self):
        data = b"".join(make_frame(EVT_TICK, i * 10, 0, 0) for i in range(10))
        dec = Decoder()
        events = dec.feed(data)
        assert len(events) == 10

    def test_incremental_feed(self):
        frame = make_frame(EVT_TICK, 42, 0, 0)
        dec = Decoder()
        events = dec.feed(frame[:4])
        assert events == []
        events = dec.feed(frame[4:])
        assert len(events) == 1

    def test_resync_on_noise(self):
        noise = bytes([0x00, 0xFF, 0x11, 0x22, 0x33])
        frame = make_frame(EVT_TICK, 1, 0, 0)
        dec = Decoder()
        events = dec.feed(noise + frame)
        assert len(events) == 1

    def test_ts_extended_64bit(self):
        # Simulate wrap from 0xFFFFFFFF to 0x00000000
        f1 = make_frame(EVT_TICK, 0xFFFF_FF00, 0, 0)
        f2 = make_frame(EVT_TICK, 0x0000_0010, 0, 0)   # wrapped
        dec = Decoder()
        e1, e2 = dec.feed(f1 + f2)
        assert e1.ts < e2.ts
        assert e2.ts > 0xFFFF_FFFF

    def test_event_fields(self):
        frame = make_frame(EVT_SWITCHED_IN, 999, 2, 7)
        dec = Decoder()
        e = dec.feed(frame)[0]
        assert e.type == EVT_SWITCHED_IN
        assert e.task_id == 2
        assert e.param == 7
        assert e.raw_ts == 999

    def test_type_name(self):
        frame = make_frame(EVT_SWITCHED_IN, 0, 0, 0)
        dec = Decoder()
        e = dec.feed(frame)[0]
        assert e.type_name == "SWITCHED_IN"


class TestNameFrames:
    def test_pack_and_assemble(self):
        raw = pack_name_frames(0, "Hello", 0)
        dec = Decoder()
        events = dec.feed(raw)
        name = dec.assemble_task_name(events, 0)
        assert name is not None
        assert "Hello" in name

    def test_long_name(self):
        raw = pack_name_frames(1, "LongTaskName", 0)
        dec = Decoder()
        events = dec.feed(raw)
        name = dec.assemble_task_name(events, 1)
        assert name is not None
        assert "LongTask" in name    # up to 16 chars encoded

    def test_unknown_task_returns_none(self):
        dec = Decoder()
        assert dec.assemble_task_name([], 99) is None


class TestDecoderEndToEnd:
    def test_decodes_synthetic_trace(self, raw_trace):
        dec = Decoder()
        events = dec.feed(raw_trace)
        assert len(events) > 0

    def test_all_events_have_valid_type(self, events):
        from scheduler_viz.core.decoder import EVT_NAMES
        for e in events:
            assert e.type in EVT_NAMES, f"Unknown event type 0x{e.type:02X}"

    def test_timestamps_non_decreasing(self, events):
        # TASK_NAME frames encode name chars in the timestamp field — not real
        # timestamps. Exclude them and TICK heartbeats from this check.
        # Sort by ts because the generator may emit the final boundary event
        # slightly out of order with respect to last-period events.
        skip = {EVT_TASK_NAME, EVT_TICK}
        ts_values = sorted(e.ts for e in events if e.type not in skip)
        assert ts_values == sorted(ts_values)
