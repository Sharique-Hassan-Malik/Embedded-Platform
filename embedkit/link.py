"""One serial link, for every host tool here that talks to a board.

Nine of these modules ship a Python program that opens a serial port, reads in
a background thread, and hands lines or frames to callbacks. Three of them
called the file `transport.py` and wrote the same class: `__init__(port, baud)`,
`close`, `__enter__`/`__exit__`, a reader thread, and an `identify()` that asks
the firmware what it is.

The differences that remain are real — one streams binary frames with a CRC,
another newline-delimited text — so both are here, chosen by the caller, rather
than one class pretending to be both.

`pyserial` is imported lazily: reading a saved capture, which is what the tests
and most analysis do, needs no serial port at all.
"""

from __future__ import annotations

import threading
import time
from dataclasses import dataclass, field
from typing import Callable, Iterable, Iterator

from .crc import crc16


class LinkError(RuntimeError):
    pass


def open_serial(port: str, baud: int, timeout: float = 1.0):
    """Open a port, with an error that says what to install if it cannot."""
    try:
        import serial  # noqa: PLC0415
    except ImportError as exc:
        raise LinkError(
            "pyserial is not installed — `pip install pyserial`. Reading a "
            "saved capture does not need it."
        ) from exc
    try:
        return serial.Serial(port, baud, timeout=timeout)
    except Exception as exc:  # noqa: BLE001 — pyserial raises several types
        raise LinkError(f"could not open {port} at {baud} baud: {exc}") from exc


@dataclass
class Link:
    """A serial connection with a background reader.

    Subclasses decide what a "message" is: `TextLink` splits on newlines,
    `FrameLink` reads length-prefixed frames with a CRC.
    """

    port: str
    baud: int = 115200
    timeout: float = 1.0

    _serial: object | None = field(default=None, init=False, repr=False)
    _thread: threading.Thread | None = field(default=None, init=False, repr=False)
    _stop: threading.Event = field(default_factory=threading.Event, init=False, repr=False)
    _handlers: list[Callable] = field(default_factory=list, init=False, repr=False)

    # -- lifecycle -----------------------------------------------------------

    def open(self) -> "Link":
        if self._serial is None:
            self._serial = open_serial(self.port, self.baud, self.timeout)
        return self

    def close(self) -> None:
        self._stop.set()
        if self._thread is not None and self._thread.is_alive():
            self._thread.join(timeout=2.0)
        if self._serial is not None:
            try:
                self._serial.close()
            finally:
                self._serial = None

    def __enter__(self) -> "Link":
        return self.open()

    def __exit__(self, *_) -> None:
        self.close()

    # -- messages ------------------------------------------------------------

    def on_message(self, handler: Callable) -> None:
        self._handlers.append(handler)

    def _deliver(self, message) -> None:
        for handler in self._handlers:
            handler(message)

    def start(self) -> None:
        """Read in the background. One thread, marked daemon so a host tool
        that forgets to close still exits."""
        if self._thread is not None:
            return
        self.open()
        self._stop.clear()
        self._thread = threading.Thread(target=self._read_loop, daemon=True)
        self._thread.start()

    def _read_loop(self) -> None:
        while not self._stop.is_set():
            try:
                for message in self.read():
                    self._deliver(message)
            except Exception:  # noqa: BLE001 — a reader thread must not die silently
                if not self._stop.is_set():
                    time.sleep(0.05)

    def read(self) -> Iterable:
        raise NotImplementedError


class TextLink(Link):
    """Newline-delimited text — what most of these firmwares actually emit."""

    def read(self) -> Iterator[str]:
        raw = self._serial.readline()
        if raw:
            yield raw.decode("utf-8", errors="replace").strip()

    def command(self, text: str, wait: float = 0.5) -> str:
        """Send a command and read one reply. `identify()` is this."""
        self.open()
        self._serial.reset_input_buffer()
        self._serial.write((text + "\n").encode())
        self._serial.flush()
        deadline = time.time() + wait
        while time.time() < deadline:
            line = self._serial.readline()
            if line:
                return line.decode("utf-8", errors="replace").strip()
        return ""

    def identify(self) -> str:
        return self.command("?")


@dataclass
class Frame:
    kind: int
    payload: bytes


class FrameLink(Link):
    """Length-prefixed binary frames with a CRC-16.

    The framing is the one the oscilloscope and the profiler firmwares already
    use: a magic byte, a kind, a 16-bit length, the payload, and a CRC over
    kind+length+payload. A frame whose CRC fails is dropped rather than
    delivered — a corrupted sample is worse than a missing one.
    """

    magic: int = 0xA5

    def read(self) -> Iterator[Frame]:
        header = self._serial.read(4)
        if len(header) < 4 or header[0] != self.magic:
            return
        kind = header[1]
        length = header[2] | (header[3] << 8)
        body = self._serial.read(length + 2)
        if len(body) < length + 2:
            return
        payload, checksum = body[:length], body[length] | (body[length + 1] << 8)
        if crc16(bytes([kind, length & 0xFF, length >> 8]) + payload) != checksum:
            return
        yield Frame(kind=kind, payload=payload)
