"""
Binary trace frame decoder.

Parses the 8-byte wire format defined in trace_protocol.h and exposes
a clean Python event stream.  Also contains the synthetic generator used
by tests and the demo mode.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import Iterator, List, Optional, Tuple

MAGIC         = 0xA5
FRAME_SIZE    = 8
FRAME_FMT     = "<BB I BB"   # magic(1) type(1) ts(4le) task_id(1) param(1)

EVT_SWITCHED_IN   = 0x01
EVT_SWITCHED_OUT  = 0x02
EVT_TASK_CREATED  = 0x03
EVT_TASK_DELETED  = 0x04
EVT_TASK_READY    = 0x05
EVT_TASK_BLOCKED  = 0x06
EVT_MUTEX_TAKEN   = 0x07
EVT_MUTEX_GIVEN   = 0x08
EVT_MUTEX_BLOCKED = 0x09
EVT_DEADLINE_MISS = 0x0A
EVT_TICK          = 0x0B
EVT_PRIO_INHERIT  = 0x0C
EVT_PRIO_RESTORE  = 0x0D
EVT_TASK_NAME     = 0x0E

EVT_NAMES = {
    EVT_SWITCHED_IN:   "SWITCHED_IN",
    EVT_SWITCHED_OUT:  "SWITCHED_OUT",
    EVT_TASK_CREATED:  "TASK_CREATED",
    EVT_TASK_DELETED:  "TASK_DELETED",
    EVT_TASK_READY:    "TASK_READY",
    EVT_TASK_BLOCKED:  "TASK_BLOCKED",
    EVT_MUTEX_TAKEN:   "MUTEX_TAKEN",
    EVT_MUTEX_GIVEN:   "MUTEX_GIVEN",
    EVT_MUTEX_BLOCKED: "MUTEX_BLOCKED",
    EVT_DEADLINE_MISS: "DEADLINE_MISS",
    EVT_TICK:          "TICK",
    EVT_PRIO_INHERIT:  "PRIO_INHERIT",
    EVT_PRIO_RESTORE:  "PRIO_RESTORE",
    EVT_TASK_NAME:     "TASK_NAME",
}

REASON_PREEMPT = 0
REASON_YIELD   = 1
REASON_BLOCK   = 2
REASON_DELETE  = 3

REASON_NAMES = {
    REASON_PREEMPT: "preempt",
    REASON_YIELD:   "yield",
    REASON_BLOCK:   "block",
    REASON_DELETE:  "delete",
}


@dataclass(frozen=True)
class TraceEvent:
    raw_ts:   int       # 32-bit tick count from wire (wraps)
    ts:       int       # 64-bit extended tick count (unwrapped by decoder)
    type:     int
    task_id:  int
    param:    int

    @property
    def type_name(self) -> str:
        return EVT_NAMES.get(self.type, f"0x{self.type:02X}")


@dataclass
class Decoder:
    """
    Stateful binary frame decoder.

    Handles UART noise by re-synchronising on the magic byte.
    Handles uint32 tick wrap-around by maintaining a 64-bit extended counter.
    Assembles TASK_NAME chunks into task name strings.
    """

    _buf:       bytes = field(default=b"", repr=False)
    _last_ts32: int   = field(default=0, repr=False)
    _ts_high:   int   = field(default=0, repr=False)
    _name_bufs: dict  = field(default_factory=dict, repr=False)  # task_id → {seq: bytes}

    def feed(self, data: bytes) -> List[TraceEvent]:
        """Append raw UART bytes and return any complete events decoded."""
        self._buf += data
        events: List[TraceEvent] = []
        while len(self._buf) >= FRAME_SIZE:
            # Re-sync: scan for magic byte
            if self._buf[0] != MAGIC:
                idx = self._buf.find(MAGIC)
                if idx == -1:
                    self._buf = b""
                    break
                self._buf = self._buf[idx:]
                continue

            frame = self._buf[:FRAME_SIZE]
            _, etype, ts32, task_id, param = struct.unpack(FRAME_FMT, frame)
            self._buf = self._buf[FRAME_SIZE:]

            ts64 = self._extend_ts(ts32)
            evt = TraceEvent(raw_ts=ts32, ts=ts64, type=etype, task_id=task_id, param=param)
            events.append(evt)

        return events

    def feed_file(self, data: bytes) -> List[TraceEvent]:
        """Decode a complete in-memory byte string."""
        return self.feed(data)

    def _extend_ts(self, ts32: int) -> int:
        """Detect uint32 wrap and extend to 64-bit."""
        if ts32 < self._last_ts32 and (self._last_ts32 - ts32) > 0x7FFF_FFFF:
            self._ts_high += 1
        self._last_ts32 = ts32
        return (self._ts_high << 32) | ts32

    def assemble_task_name(self, events: List[TraceEvent], task_id: int) -> Optional[str]:
        """
        Collect TASK_NAME chunk events for task_id and return the full name,
        or None if incomplete.
        """
        chunks: dict[int, bytes] = {}
        for e in events:
            if e.type == EVT_TASK_NAME and e.task_id == task_id:
                seq = e.param
                raw = struct.pack("<I", e.raw_ts)
                chunks[seq] = raw
        if not chunks:
            return None
        assembled = b""
        for seq in sorted(chunks):
            assembled += chunks[seq]
        return assembled.rstrip(b"\x00").decode("ascii", errors="replace")


def pack_frame(
    etype: int,
    ts: int,
    task_id: int,
    param: int,
) -> bytes:
    """Encode a single 8-byte trace frame (used by the synthetic generator)."""
    return struct.pack(FRAME_FMT, MAGIC, etype, ts & 0xFFFF_FFFF, task_id, param)


def pack_name_frames(task_id: int, name: str, base_ts: int) -> bytes:
    """Encode a task name as one or more TASK_NAME frames."""
    encoded = name.encode("ascii", errors="replace")[:16]
    out = b""
    for seq in range(0, len(encoded), 4):
        chunk = encoded[seq : seq + 4].ljust(4, b"\x00")
        ts_val = struct.unpack("<I", chunk)[0]
        out += struct.pack(FRAME_FMT, MAGIC, EVT_TASK_NAME, ts_val, task_id, seq // 4)
    return out
