#!/usr/bin/env python3
"""
decode.py — Host-side decoder and live display for the PIC24 protocol sniffer.

Modes:
  live    Stream from serial port and render a live decode view in the terminal.
  sd      Read raw sector log from an SD card image and decode to a text file.

Usage:
    # Live decode (default: 115200 baud on /dev/ttyUSB0):
    python3 decode.py live --port /dev/ttyUSB0

    # Live decode with specific port and baud:
    python3 decode.py live --port COM4 --baud 115200

    # Change sniffer UART baud rate to 9600 via control channel:
    python3 decode.py live --port /dev/ttyUSB0 --sniff-baud 9600

    # Change SPI mode to 3:
    python3 decode.py live --port /dev/ttyUSB0 --spi-mode 3

    # Decode SD card log:
    python3 decode.py sd --image /dev/sdX --out capture.txt
    python3 decode.py sd --image sd_dump.bin --out capture.txt

Requires: pyserial (for live mode)
    pip install pyserial
"""

import argparse
import struct
import sys
import os
import time
import threading
from collections import deque

try:
    import serial
    _SERIAL_OK = True
except ImportError:
    _SERIAL_OK = False


# ---- SD log constants (must match firmware sdlog.h) --------------------- #

SDLOG_REC_SIZE   = 16
SDLOG_RECS_PER_S = 32
SDLOG_START_SEC  = 2048
SECTOR_SIZE      = 512


# ---- Frame parser -------------------------------------------------------- #

class Frame:
    """One decoded capture event."""
    __slots__ = ("ts_us", "proto", "flags", "meta", "data",
                 "label", "detail", "error")

    PROTO_UART = 1
    PROTO_I2C  = 2
    PROTO_SPI  = 3

    FLAG_UART_FE       = 0x1
    FLAG_I2C_START     = 0x1
    FLAG_I2C_STOP      = 0x2
    FLAG_I2C_ACK       = 0x4
    FLAG_I2C_NACK      = 0x8
    FLAG_I2C_ADDR      = 0x2
    FLAG_SPI_CS_ASSERT = 0x1
    FLAG_SPI_CS_DEASS  = 0x2
    FLAG_SPI_MOSI      = 0x4
    FLAG_SPI_MISO      = 0x8

    def __init__(self, ts_us, cap_word):
        self.ts_us  = ts_us
        self.proto  = (cap_word >> 28) & 0xF
        self.flags  = (cap_word >> 24) & 0xF
        self.meta   = (cap_word >>  8) & 0xFFFF
        self.data   = cap_word & 0xFF
        self.error  = False
        self._decode()

    def _decode(self):
        p, f, d = self.proto, self.flags, self.data
        if p == self.PROTO_UART:
            self.label  = "UART"
            self.detail = f"0x{d:02X} '{_safe_char(d)}'"
            if f & self.FLAG_UART_FE:
                self.detail += " [FRAMING ERROR]"
                self.error = True
        elif p == self.PROTO_I2C:
            self.label = "I2C"
            if f & self.FLAG_I2C_START:
                self.detail = "START"
            elif f & self.FLAG_I2C_STOP:
                self.detail = "STOP"
            elif f & self.FLAG_I2C_ADDR:
                addr = d >> 1
                rw   = "R" if d & 1 else "W"
                ack  = "NAK" if f & self.FLAG_I2C_NACK else "ACK"
                self.detail = f"ADDR 0x{addr:02X} {rw} {ack}"
                self.error  = bool(f & self.FLAG_I2C_NACK)
            else:
                ack = "NAK" if f & self.FLAG_I2C_NACK else "ACK"
                self.detail = f"DATA 0x{d:02X} '{_safe_char(d)}' {ack}"
                self.error  = bool(f & self.FLAG_I2C_NACK)
        elif p == self.PROTO_SPI:
            self.label = "SPI"
            if f & self.FLAG_SPI_CS_ASSERT:
                self.detail = "/CS LOW"
            elif f & self.FLAG_SPI_CS_DEASS:
                self.detail = "/CS HIGH"
            elif f & self.FLAG_SPI_MOSI:
                self.detail = f"MOSI 0x{d:02X} '{_safe_char(d)}'"
            elif f & self.FLAG_SPI_MISO:
                self.detail = f"MISO 0x{d:02X} '{_safe_char(d)}'"
            else:
                self.detail = f"? 0x{d:02X}"
        else:
            self.label  = "???"
            self.detail = f"raw=0x{(p<<28|(f<<24)|d):08X}"
            self.error  = True

    def __str__(self):
        ts_ms = self.ts_us / 1000.0
        err   = " !" if self.error else "  "
        return f"{ts_ms:12.3f} ms  {self.label:4s}{err} {self.detail}"


def _safe_char(b):
    return chr(b) if 0x20 <= b <= 0x7E else "."


# ---- Packet reconstructor ----------------------------------------------- #

class PacketReconstructor:
    """
    Accumulates Frame objects and groups them into logical packets.

    I2C: groups frames from START to STOP as one transaction.
    SPI: groups frames from /CS LOW to /CS HIGH as one transaction.
    UART: groups consecutive bytes within a configurable inter-byte gap.
    """

    def __init__(self, uart_gap_us=5000):
        self._uart_gap = uart_gap_us
        self._i2c_pkt  = []
        self._spi_pkt  = []
        self._uart_pkt = []
        self._uart_last_ts = 0
        self._packets  = deque()

    def feed(self, frame):
        p, f = frame.proto, frame.flags

        if p == Frame.PROTO_I2C:
            if f & Frame.FLAG_I2C_START:
                self._i2c_pkt = [frame]
            elif self._i2c_pkt:
                self._i2c_pkt.append(frame)
                if f & Frame.FLAG_I2C_STOP:
                    self._packets.append(("I2C", list(self._i2c_pkt)))
                    self._i2c_pkt = []

        elif p == Frame.PROTO_SPI:
            if f & Frame.FLAG_SPI_CS_ASSERT:
                self._spi_pkt = [frame]
            elif self._spi_pkt:
                self._spi_pkt.append(frame)
                if f & Frame.FLAG_SPI_CS_DEASS:
                    self._packets.append(("SPI", list(self._spi_pkt)))
                    self._spi_pkt = []

        elif p == Frame.PROTO_UART:
            gap = frame.ts_us - self._uart_last_ts
            if self._uart_pkt and gap > self._uart_gap:
                self._packets.append(("UART", list(self._uart_pkt)))
                self._uart_pkt = []
            self._uart_pkt.append(frame)
            self._uart_last_ts = frame.ts_us

    def flush_uart(self):
        """Force-emit any pending UART packet (call periodically)."""
        if self._uart_pkt:
            self._packets.append(("UART", list(self._uart_pkt)))
            self._uart_pkt = []

    def get_packet(self):
        return self._packets.popleft() if self._packets else None


def format_packet(proto, frames):
    lines = [f"── {proto} transaction ─────────────────────────────"]
    for f in frames:
        lines.append(f"  {f}")
    if proto == "I2C":
        data_frames = [f for f in frames
                       if f.flags & ~(Frame.FLAG_I2C_START | Frame.FLAG_I2C_STOP)
                       and f.proto == Frame.PROTO_I2C
                       and not (f.flags & Frame.FLAG_I2C_ADDR)
                       and not (f.flags & Frame.FLAG_I2C_START)
                       and not (f.flags & Frame.FLAG_I2C_STOP)]
        if data_frames:
            raw = bytes(f.data for f in data_frames)
            lines.append(f"  hex: {raw.hex()}")
            printable = "".join(_safe_char(b) for b in raw)
            lines.append(f"  ascii: {printable}")
    elif proto == "SPI":
        mosi = bytes(f.data for f in frames if f.flags & Frame.FLAG_SPI_MOSI)
        miso = bytes(f.data for f in frames if f.flags & Frame.FLAG_SPI_MISO)
        if mosi: lines.append(f"  MOSI: {mosi.hex()}")
        if miso: lines.append(f"  MISO: {miso.hex()}")
    elif proto == "UART":
        raw = bytes(f.data for f in frames)
        lines.append(f"  hex: {raw.hex()}")
        printable = "".join(_safe_char(b) for b in raw)
        lines.append(f"  ascii: {printable}")
    return "\n".join(lines)


# ---- Live serial line parser -------------------------------------------- #

def parse_serial_line(line, recon):
    """
    Parse one text line from the firmware UART output.
    Format: "TTTTTTTT U|I|S ..."
    """
    parts = line.strip().split()
    if not parts or len(parts[0]) != 8:
        return None
    try:
        ts_us = int(parts[0], 16)
    except ValueError:
        return None

    if len(parts) < 2:
        return None

    proto_char = parts[1]

    # Reconstruct a capture word from the text frame.
    cap_word = 0

    if proto_char == "U":
        cap_word = (Frame.PROTO_UART << 28)
        if len(parts) >= 3:
            cap_word |= int(parts[2], 16)
        if len(parts) >= 4 and parts[3] == "FE":
            cap_word |= (Frame.FLAG_UART_FE << 24)

    elif proto_char == "I":
        cap_word = (Frame.PROTO_I2C << 28)
        if len(parts) >= 3:
            sub = parts[2]
            if sub == "S":
                cap_word |= (Frame.FLAG_I2C_START << 24)
            elif sub == "P":
                cap_word |= (Frame.FLAG_I2C_STOP << 24)
            elif sub == "A" and len(parts) >= 6:
                addr = int(parts[3], 16)
                rw   = 1 if parts[4] == "R" else 0
                ack  = Frame.FLAG_I2C_NACK if parts[5] == "NAK" else Frame.FLAG_I2C_ACK
                cap_word |= (Frame.FLAG_I2C_ADDR << 24) | (ack << 24)
                cap_word |= ((addr << 1) | rw)
            elif sub == "D" and len(parts) >= 4:
                data = int(parts[3], 16)
                ack  = Frame.FLAG_I2C_NACK if (len(parts) >= 5 and parts[4] == "NAK") \
                       else Frame.FLAG_I2C_ACK
                cap_word |= (ack << 24) | data

    elif proto_char == "S":
        cap_word = (Frame.PROTO_SPI << 28)
        if len(parts) >= 3:
            sub = parts[2]
            if sub == "CS":
                cap_word |= (Frame.FLAG_SPI_CS_ASSERT << 24)
            elif sub == "CD":
                cap_word |= (Frame.FLAG_SPI_CS_DEASS << 24)
            elif sub == "O" and len(parts) >= 4:
                cap_word |= (Frame.FLAG_SPI_MOSI << 24) | int(parts[3], 16)
            elif sub == "I" and len(parts) >= 4:
                cap_word |= (Frame.FLAG_SPI_MISO << 24) | int(parts[3], 16)

    frame = Frame(ts_us, cap_word)
    recon.feed(frame)
    return frame


# ---- Sub-command: live -------------------------------------------------- #

def cmd_live(args):
    if not _SERIAL_OK:
        sys.exit("Install pyserial: pip install pyserial")

    baud_map = {"9600": "B0", "19200": "B1", "38400": "B2", "57600": "B3",
                "115200": "B4", "250000": "B5", "500000": "B6"}

    with serial.Serial(args.port, 115200, timeout=0.1) as ser:
        print(f"Connected to {args.port}")

        # Send control commands if requested.
        if args.sniff_baud:
            key = str(args.sniff_baud)
            if key in baud_map:
                ser.write(baud_map[key].encode())
                time.sleep(0.1)
                print(f"  Sniffer UART baud → {args.sniff_baud}")
        if args.spi_mode is not None:
            ser.write(f"M{args.spi_mode}".encode())
            time.sleep(0.1)
            print(f"  SPI mode → {args.spi_mode}")

        recon = PacketReconstructor()
        buf   = b""
        last_flush = time.time()

        while True:
            chunk = ser.read(256)
            if chunk:
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    text = line.decode("ascii", errors="replace")
                    if text.startswith("#"):
                        print(f"  {text}")
                        continue
                    frame = parse_serial_line(text, recon)
                    if frame and args.verbose:
                        print(frame)

            # Flush UART accumulator periodically.
            now = time.time()
            if now - last_flush > 0.05:
                recon.flush_uart()
                last_flush = now

            # Drain packet queue.
            while True:
                pkt = recon.get_packet()
                if pkt is None:
                    break
                proto, frames = pkt
                print(format_packet(proto, frames))
                print()


# ---- Sub-command: sd ---------------------------------------------------- #

def cmd_sd(args):
    print(f"Reading SD image: {args.image}")
    frames  = []
    recon   = PacketReconstructor()

    with open(args.image, "rb") as f:
        f.seek(SDLOG_START_SEC * SECTOR_SIZE)
        sector_count = 0

        while sector_count < 65000:
            sector = f.read(SECTOR_SIZE)
            if len(sector) < SECTOR_SIZE:
                break

            all_ff = all(b == 0xFF for b in sector)
            all_zero = all(b == 0 for b in sector)
            if all_ff or all_zero:
                break   # end of log

            for rec_idx in range(SDLOG_RECS_PER_S):
                off = rec_idx * SDLOG_REC_SIZE
                rec = sector[off : off + SDLOG_REC_SIZE]
                ts_us    = struct.unpack_from(">I", rec, 0)[0]
                cap_word = struct.unpack_from(">I", rec, 4)[0]
                if cap_word == 0:
                    continue
                frame = Frame(ts_us, cap_word)
                frames.append(frame)
                recon.feed(frame)

            sector_count += 1

    recon.flush_uart()

    packets = []
    while True:
        pkt = recon.get_packet()
        if pkt is None:
            break
        packets.append(pkt)

    out_path = args.out or "capture.txt"
    with open(out_path, "w") as out:
        out.write(f"# PIC24 Protocol Sniffer capture — {len(frames)} events, "
                  f"{len(packets)} packets\n\n")
        for proto, pkt_frames in packets:
            out.write(format_packet(proto, pkt_frames))
            out.write("\n\n")

    print(f"  {len(frames)} events decoded into {len(packets)} packets → {out_path}")


# ---- Entry point --------------------------------------------------------- #

def main():
    p = argparse.ArgumentParser(description="PIC24 protocol sniffer host decoder")
    sub = p.add_subparsers(dest="cmd", required=True)

    lv = sub.add_parser("live", help="Live decode from serial port")
    lv.add_argument("--port",        default="/dev/ttyUSB0")
    lv.add_argument("--sniff-baud",  type=int, default=None,
                    help="Set sniffer UART baud (9600..500000)")
    lv.add_argument("--spi-mode",    type=int, choices=[0, 3], default=None)
    lv.add_argument("--verbose",     action="store_true",
                    help="Print every raw frame in addition to packets")

    sd = sub.add_parser("sd", help="Decode SD card raw log")
    sd.add_argument("--image", required=True, help="SD card device or image file")
    sd.add_argument("--out",   default=None,  help="Output text file (default: capture.txt)")

    args = p.parse_args()
    {"live": cmd_live, "sd": cmd_sd}[args.cmd](args)


if __name__ == "__main__":
    main()
