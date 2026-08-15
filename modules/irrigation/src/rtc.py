# rtc.py — DS3231 real-time clock driver over I2C.
# Implements BCD encode/decode and register access from scratch.
# No MicroPython RTC library is used; only machine.I2C for raw byte transfers.

from machine import I2C, Pin
import config


def _bcd_to_dec(b: int) -> int:
    return (b >> 4) * 10 + (b & 0x0F)


def _dec_to_bcd(d: int) -> int:
    return ((d // 10) << 4) | (d % 10)


class DS3231:
    """
    DS3231 register map (datasheet section 8.2):
      0x00  Seconds   BCD 00–59
      0x01  Minutes   BCD 00–59
      0x02  Hours     BCD 00–23 (bit 6 = 0 for 24-hour mode)
      0x03  Day       BCD 01–07
      0x04  Date      BCD 01–31
      0x05  Month     BCD 01–12
      0x06  Year      BCD 00–99  (offset from 2000)
    """

    _REG_SECONDS = const(0x00)

    def __init__(self) -> None:
        self._i2c = I2C(0, sda=Pin(config.I2C_SDA), scl=Pin(config.I2C_SCL),
                        freq=400_000)
        self._addr = config.RTC_ADDR

    def _read_regs(self, reg: int, n: int) -> bytes:
        self._i2c.writeto(self._addr, bytes([reg]))
        return self._i2c.readfrom(self._addr, n)

    def _write_regs(self, reg: int, data: bytes) -> None:
        self._i2c.writeto(self._addr, bytes([reg]) + data)

    def now(self) -> tuple:
        """Return (year, month, day, hour, minute, second, weekday)."""
        raw = self._read_regs(self._REG_SECONDS, 7)
        sec  = _bcd_to_dec(raw[0] & 0x7F)
        mn   = _bcd_to_dec(raw[1] & 0x7F)
        hr   = _bcd_to_dec(raw[2] & 0x3F)
        # weekday raw[3]: 1–7; convert to 0–6 (Monday = 0)
        wd   = (raw[3] & 0x07) - 1
        day  = _bcd_to_dec(raw[4] & 0x3F)
        mon  = _bcd_to_dec(raw[5] & 0x1F)
        yr   = _bcd_to_dec(raw[6]) + 2000
        return (yr, mon, day, hr, mn, sec, wd)

    def set_time(self, year: int, month: int, day: int,
                 hour: int, minute: int, second: int, weekday: int = 1) -> None:
        """Set the RTC. weekday: 1–7 (1 = Monday)."""
        data = bytes([
            _dec_to_bcd(second),
            _dec_to_bcd(minute),
            _dec_to_bcd(hour) & 0x3F,   # ensure 24-hour mode (bit 6 = 0)
            weekday & 0x07,
            _dec_to_bcd(day),
            _dec_to_bcd(month),
            _dec_to_bcd(year - 2000),
        ])
        self._write_regs(self._REG_SECONDS, data)

    def temperature(self) -> float:
        """DS3231 internal temperature sensor (±3 °C accuracy, 0.25 °C resolution)."""
        # Registers 0x11 (MSB integer) and 0x12 (bits 7:6 = fractional × 0.25)
        raw = self._read_regs(0x11, 2)
        msb = raw[0]
        frac = (raw[1] >> 6) * 0.25
        # MSB is a signed 8-bit integer.
        temp = (msb if msb < 128 else msb - 256) + frac
        return temp
