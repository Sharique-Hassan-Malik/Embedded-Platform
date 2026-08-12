"""
UART transport for live capture from a connected MCU.

Uses pyserial when available.  Falls back to a file-based replay mode
so the rest of the tool works without hardware attached.
"""

from __future__ import annotations

import threading
import time
from typing import Callable, List, Optional

from scheduler_viz.core.decoder import Decoder, TraceEvent


class SerialTransport:
    """
    Read raw trace bytes from a UART port and decode them on a background
    thread.  Each batch of decoded events is passed to `on_events`.

    Usage:
        transport = SerialTransport("/dev/ttyUSB0", 921600, on_events=handler)
        transport.start()
        ...
        transport.stop()
    """

    def __init__(
        self,
        port: str,
        baud: int = 921600,
        on_events: Optional[Callable[[List[TraceEvent]], None]] = None,
        read_timeout: float = 0.05,
    ) -> None:
        self.port = port
        self.baud = baud
        self.on_events = on_events or (lambda evts: None)
        self.read_timeout = read_timeout
        self._decoder = Decoder()
        self._thread: Optional[threading.Thread] = None
        self._stop = threading.Event()
        self._serial = None

    def start(self) -> None:
        try:
            import serial as pyserial
            self._serial = pyserial.Serial(
                self.port, self.baud, timeout=self.read_timeout
            )
        except ImportError:
            raise RuntimeError(
                "pyserial is required for live capture: pip install pyserial"
            )
        except Exception as exc:
            raise RuntimeError(f"Cannot open {self.port}: {exc}") from exc

        self._stop.clear()
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2.0)
        if self._serial and self._serial.is_open:
            self._serial.close()

    def _loop(self) -> None:
        while not self._stop.is_set():
            try:
                chunk = self._serial.read(256)
                if chunk:
                    events = self._decoder.feed(chunk)
                    if events:
                        self.on_events(events)
            except Exception:
                if not self._stop.is_set():
                    time.sleep(0.1)


class FileReplayTransport:
    """
    Replay a saved binary trace file as if it were coming from a live port.
    Useful for offline analysis and demos.
    """

    def __init__(
        self,
        path: str,
        on_events: Optional[Callable[[List[TraceEvent]], None]] = None,
    ) -> None:
        self.path = path
        self.on_events = on_events or (lambda evts: None)
        self._decoder = Decoder()

    def run(self) -> List[TraceEvent]:
        """Decode the entire file and return all events. Also calls on_events."""
        with open(self.path, "rb") as fh:
            data = fh.read()
        events = self._decoder.feed(data)
        if events:
            self.on_events(events)
        return events
