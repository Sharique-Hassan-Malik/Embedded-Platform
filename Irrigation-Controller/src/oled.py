# oled.py — SSD1306 128×64 OLED driver over I2C.
# Implements the initialization sequence, page-addressed framebuffer write
# and a minimal 5×7 ASCII font renderer from scratch.
# Only machine.I2C is used as a transport primitive.

from machine import I2C, Pin
import config

# ── SSD1306 command constants (datasheet section 8) ──────────────────────────
_CMD_DISPLAY_OFF      = const(0xAE)
_CMD_DISPLAY_ON       = const(0xAF)
_CMD_SET_CONTRAST     = const(0x81)
_CMD_ENTIRE_ON        = const(0xA4)
_CMD_NORM_INV         = const(0xA6)
_CMD_MEM_ADDR_MODE    = const(0x20)
_CMD_COL_ADDR         = const(0x21)
_CMD_PAGE_ADDR        = const(0x22)
_CMD_SEG_REMAP        = const(0xA1)
_CMD_COM_SCAN_DEC     = const(0xC8)
_CMD_SET_DISP_OFFSET  = const(0xD3)
_CMD_SET_COM_PINS     = const(0xDA)
_CMD_SET_DISP_CLK     = const(0xD5)
_CMD_SET_PRECHARGE    = const(0xD9)
_CMD_SET_VCOM_DESEL   = const(0xDB)
_CMD_CHARGEPUMP       = const(0x8D)
_CMD_SET_MULTIPLEX    = const(0xA8)
_CMD_SET_DISP_START   = const(0x40)

# ── 5×7 ASCII font (printable characters 0x20–0x7E) ──────────────────────────
# Each character is 5 bytes wide; each byte is a column of 7 pixels (bit 0 = top).
_FONT = (
    b'\x00\x00\x00\x00\x00'  # 0x20 space
    b'\x00\x00\x5f\x00\x00'  # 0x21 !
    b'\x00\x07\x00\x07\x00'  # 0x22 "
    b'\x14\x7f\x14\x7f\x14'  # 0x23 #
    b'\x24\x2a\x7f\x2a\x12'  # 0x24 $
    b'\x23\x13\x08\x64\x62'  # 0x25 %
    b'\x36\x49\x55\x22\x50'  # 0x26 &
    b'\x00\x05\x03\x00\x00'  # 0x27 '
    b'\x00\x1c\x22\x41\x00'  # 0x28 (
    b'\x00\x41\x22\x1c\x00'  # 0x29 )
    b'\x14\x08\x3e\x08\x14'  # 0x2a *
    b'\x08\x08\x3e\x08\x08'  # 0x2b +
    b'\x00\x50\x30\x00\x00'  # 0x2c ,
    b'\x08\x08\x08\x08\x08'  # 0x2d -
    b'\x00\x60\x60\x00\x00'  # 0x2e .
    b'\x20\x10\x08\x04\x02'  # 0x2f /
    b'\x3e\x51\x49\x45\x3e'  # 0x30 0
    b'\x00\x42\x7f\x40\x00'  # 0x31 1
    b'\x42\x61\x51\x49\x46'  # 0x32 2
    b'\x21\x41\x45\x4b\x31'  # 0x33 3
    b'\x18\x14\x12\x7f\x10'  # 0x34 4
    b'\x27\x45\x45\x45\x39'  # 0x35 5
    b'\x3c\x4a\x49\x49\x30'  # 0x36 6
    b'\x01\x71\x09\x05\x03'  # 0x37 7
    b'\x36\x49\x49\x49\x36'  # 0x38 8
    b'\x06\x49\x49\x29\x1e'  # 0x39 9
    b'\x00\x36\x36\x00\x00'  # 0x3a :
    b'\x00\x56\x36\x00\x00'  # 0x3b ;
    b'\x08\x14\x22\x41\x00'  # 0x3c <
    b'\x14\x14\x14\x14\x14'  # 0x3d =
    b'\x00\x41\x22\x14\x08'  # 0x3e >
    b'\x02\x01\x51\x09\x06'  # 0x3f ?
    b'\x32\x49\x79\x41\x3e'  # 0x40 @
    b'\x7e\x11\x11\x11\x7e'  # 0x41 A
    b'\x7f\x49\x49\x49\x36'  # 0x42 B
    b'\x3e\x41\x41\x41\x22'  # 0x43 C
    b'\x7f\x41\x41\x22\x1c'  # 0x44 D
    b'\x7f\x49\x49\x49\x41'  # 0x45 E
    b'\x7f\x09\x09\x09\x01'  # 0x46 F
    b'\x3e\x41\x49\x49\x7a'  # 0x47 G
    b'\x7f\x08\x08\x08\x7f'  # 0x48 H
    b'\x00\x41\x7f\x41\x00'  # 0x49 I
    b'\x20\x40\x41\x3f\x01'  # 0x4a J
    b'\x7f\x08\x14\x22\x41'  # 0x4b K
    b'\x7f\x40\x40\x40\x40'  # 0x4c L
    b'\x7f\x02\x0c\x02\x7f'  # 0x4d M
    b'\x7f\x04\x08\x10\x7f'  # 0x4e N
    b'\x3e\x41\x41\x41\x3e'  # 0x4f O
    b'\x7f\x09\x09\x09\x06'  # 0x50 P
    b'\x3e\x41\x51\x21\x5e'  # 0x51 Q
    b'\x7f\x09\x19\x29\x46'  # 0x52 R
    b'\x46\x49\x49\x49\x31'  # 0x53 S
    b'\x01\x01\x7f\x01\x01'  # 0x54 T
    b'\x3f\x40\x40\x40\x3f'  # 0x55 U
    b'\x1f\x20\x40\x20\x1f'  # 0x56 V
    b'\x3f\x40\x38\x40\x3f'  # 0x57 W
    b'\x63\x14\x08\x14\x63'  # 0x58 X
    b'\x07\x08\x70\x08\x07'  # 0x59 Y
    b'\x61\x51\x49\x45\x43'  # 0x5a Z
    b'\x00\x7f\x41\x41\x00'  # 0x5b [
    b'\x02\x04\x08\x10\x20'  # 0x5c backslash
    b'\x00\x41\x41\x7f\x00'  # 0x5d ]
    b'\x04\x02\x01\x02\x04'  # 0x5e ^
    b'\x40\x40\x40\x40\x40'  # 0x5f _
    b'\x00\x01\x02\x04\x00'  # 0x60 `
    b'\x20\x54\x54\x54\x78'  # 0x61 a
    b'\x7f\x48\x44\x44\x38'  # 0x62 b
    b'\x38\x44\x44\x44\x20'  # 0x63 c
    b'\x38\x44\x44\x48\x7f'  # 0x64 d
    b'\x38\x54\x54\x54\x18'  # 0x65 e
    b'\x08\x7e\x09\x01\x02'  # 0x66 f
    b'\x0c\x52\x52\x52\x3e'  # 0x67 g
    b'\x7f\x08\x04\x04\x78'  # 0x68 h
    b'\x00\x44\x7d\x40\x00'  # 0x69 i
    b'\x20\x40\x44\x3d\x00'  # 0x6a j
    b'\x7f\x10\x28\x44\x00'  # 0x6b k
    b'\x00\x41\x7f\x40\x00'  # 0x6c l
    b'\x7c\x04\x18\x04\x78'  # 0x6d m
    b'\x7c\x08\x04\x04\x78'  # 0x6e n
    b'\x38\x44\x44\x44\x38'  # 0x6f o
    b'\x7c\x14\x14\x14\x08'  # 0x70 p
    b'\x08\x14\x14\x18\x7c'  # 0x71 q
    b'\x7c\x08\x04\x04\x08'  # 0x72 r
    b'\x48\x54\x54\x54\x20'  # 0x73 s
    b'\x04\x3f\x44\x40\x20'  # 0x74 t
    b'\x3c\x40\x40\x20\x7c'  # 0x75 u
    b'\x1c\x20\x40\x20\x1c'  # 0x76 v
    b'\x3c\x40\x30\x40\x3c'  # 0x77 w
    b'\x44\x28\x10\x28\x44'  # 0x78 x
    b'\x0c\x50\x50\x50\x3c'  # 0x79 y
    b'\x44\x64\x54\x4c\x44'  # 0x7a z
    b'\x00\x08\x36\x41\x00'  # 0x7b {
    b'\x00\x00\x7f\x00\x00'  # 0x7c |
    b'\x00\x41\x36\x08\x00'  # 0x7d }
    b'\x10\x08\x08\x10\x08'  # 0x7e ~
)


class SSD1306:
    """128×64 monochrome OLED over I2C. Page-addressed, column-major framebuffer."""

    WIDTH  = config.OLED_WIDTH
    HEIGHT = config.OLED_HEIGHT
    PAGES  = HEIGHT // 8   # 8 pages of 8 rows each

    def __init__(self) -> None:
        self._i2c   = I2C(0, sda=Pin(config.I2C_SDA), scl=Pin(config.I2C_SCL),
                          freq=400_000)
        self._addr  = config.OLED_ADDR
        self._buf   = bytearray(self.WIDTH * self.PAGES)
        self._init_display()

    def _cmd(self, *cmds: int) -> None:
        # Control byte 0x00 signals command stream to the SSD1306.
        self._i2c.writeto(self._addr, bytes([0x00]) + bytes(cmds))

    def _data(self, data: bytes) -> None:
        # Control byte 0x40 signals data (GDDRAM write) stream.
        self._i2c.writeto(self._addr, bytes([0x40]) + data)

    def _init_display(self) -> None:
        self._cmd(
            _CMD_DISPLAY_OFF,
            _CMD_SET_DISP_CLK, 0x80,
            _CMD_SET_MULTIPLEX, self.HEIGHT - 1,
            _CMD_SET_DISP_OFFSET, 0x00,
            _CMD_SET_DISP_START,
            _CMD_CHARGEPUMP, 0x14,       # enable internal charge pump
            _CMD_MEM_ADDR_MODE, 0x00,    # horizontal addressing mode
            _CMD_SEG_REMAP,              # column address 127 mapped to SEG0
            _CMD_COM_SCAN_DEC,           # scan from COM[N-1] to COM0
            _CMD_SET_COM_PINS, 0x12,
            _CMD_SET_CONTRAST, 0xCF,
            _CMD_SET_PRECHARGE, 0xF1,
            _CMD_SET_VCOM_DESEL, 0x40,
            _CMD_ENTIRE_ON,
            _CMD_NORM_INV,
            _CMD_DISPLAY_ON,
        )

    def fill(self, colour: int = 0) -> None:
        v = 0xFF if colour else 0x00
        for i in range(len(self._buf)):
            self._buf[i] = v

    def pixel(self, x: int, y: int, colour: int = 1) -> None:
        if 0 <= x < self.WIDTH and 0 <= y < self.HEIGHT:
            page = y // 8
            bit  = y % 8
            idx  = page * self.WIDTH + x
            if colour:
                self._buf[idx] |= (1 << bit)
            else:
                self._buf[idx] &= ~(1 << bit)

    def text(self, s: str, x: int, y: int, colour: int = 1) -> None:
        """Render a string at pixel position (x, y) using the 5×7 font."""
        for ch in s:
            code = ord(ch)
            if 0x20 <= code <= 0x7E:
                offset = (code - 0x20) * 5
                glyph  = _FONT[offset:offset + 5]
                for col_idx, col_byte in enumerate(glyph):
                    for row in range(7):
                        if col_byte & (1 << row):
                            self.pixel(x + col_idx, y + row, colour)
            x += 6   # 5 px glyph + 1 px spacing

    def hline(self, x: int, y: int, w: int, colour: int = 1) -> None:
        for i in range(w):
            self.pixel(x + i, y, colour)

    def show(self) -> None:
        """Flush the framebuffer to the display over I2C."""
        self._cmd(
            _CMD_COL_ADDR,  0, self.WIDTH - 1,
            _CMD_PAGE_ADDR, 0, self.PAGES - 1,
        )
        # Send in 16-byte chunks to stay within I2C buffer limits.
        view = memoryview(self._buf)
        chunk = 16
        for start in range(0, len(self._buf), chunk):
            self._data(bytes(view[start:start + chunk]))
