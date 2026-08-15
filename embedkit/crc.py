"""CRC-16/CCITT, once.

Three firmwares and two host tools compute this, because a serial link without
a check is a serial link that hands you a corrupted sample and says nothing.
The polynomial and the initial value have to match on both sides of the wire,
which is exactly the kind of pair that drifts when it is written twice.
"""

from __future__ import annotations

POLYNOMIAL = 0x1021
INITIAL = 0xFFFF


def crc16(data: bytes, initial: int = INITIAL) -> int:
    """CRC-16/CCITT-FALSE: polynomial 0x1021, initial 0xFFFF, no reflection.

    The variant matters. CCITT-FALSE and the "true" CCITT differ only in the
    initial value, produce different results for every input, and are both
    called "CRC-16 CCITT" in datasheets.
    """
    crc = initial
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ POLYNOMIAL) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def check(data: bytes, expected: int) -> bool:
    return crc16(data) == expected
