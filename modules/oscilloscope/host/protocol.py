"""Wire protocol constants — mirrors firmware/include/protocol.h."""

from __future__ import annotations
import struct

# ── Command bytes ──────────────────────────────────────────────────────────────
CMD_START       = 0x01
CMD_STOP        = 0x02
CMD_SET_RATE    = 0x03
CMD_SET_CHANNEL = 0x04
CMD_SET_SAMPLES = 0x05
CMD_PING        = 0x06

# ── Frame constants ────────────────────────────────────────────────────────────
FRAME_SYNC     = bytes([0xDE, 0xAD, 0xC0, 0xDE])
FRAME_SYNC_LEN = 4
FRAME_HDR_LEN  = 8   # sync(4) + channel(1) + flags(1) + n_samples(2)
FRAME_FTR_LEN  = 2   # CRC16
FRAME_FLAG_OVERFLOW = 0x01

PONG_BYTE        = 0xAC
FIRMWARE_VERSION = 0x00010000

ADC_VREF_MV  = 3300
ADC_BITS     = 12
ADC_FULL_SCALE = (1 << ADC_BITS) - 1   # 4095

DEFAULT_CLKDIV   = 960
DEFAULT_CHANNEL  = 0
DEFAULT_N_SAMPLES = 1024
MIN_CLKDIV = 96
MAX_CLKDIV = 65535

ADC_CLOCK_HZ = 48_000_000   # Pico USB PLL → ADC clock


def clkdiv_to_sps(clkdiv: int) -> float:
    """Convert ADC clock divisor to samples per second."""
    return ADC_CLOCK_HZ / max(clkdiv, MIN_CLKDIV)


def sps_to_clkdiv(sps: float) -> int:
    """Convert desired sample rate (sps) to the nearest valid clock divisor."""
    div = round(ADC_CLOCK_HZ / sps)
    return max(MIN_CLKDIV, min(MAX_CLKDIV, div))


def raw_to_mv(raw12: int) -> float:
    """Convert 12-bit ADC reading to millivolts."""
    return raw12 * ADC_VREF_MV / ADC_FULL_SCALE


# ── CRC-16/CCITT-FALSE ─────────────────────────────────────────────────────────
def _crc16_update(crc: int, byte: int) -> int:
    crc ^= byte << 8
    for _ in range(8):
        crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def crc16(data: bytes | bytearray) -> int:
    crc = 0xFFFF
    for b in data:
        crc = _crc16_update(crc, b)
    return crc


# ── Command builders ───────────────────────────────────────────────────────────
def cmd_start() -> bytes:
    return bytes([CMD_START])

def cmd_stop() -> bytes:
    return bytes([CMD_STOP])

def cmd_set_rate(clkdiv: int) -> bytes:
    return bytes([CMD_SET_RATE]) + struct.pack("<I", clkdiv)

def cmd_set_channel(ch: int) -> bytes:
    return bytes([CMD_SET_CHANNEL, ch & 0x03])

def cmd_set_samples(n: int) -> bytes:
    return bytes([CMD_SET_SAMPLES]) + struct.pack("<H", n)

def cmd_ping() -> bytes:
    return bytes([CMD_PING])
