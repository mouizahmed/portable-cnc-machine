# ili9488.py – ILI9488 480×320 SPI TFT driver for MicroPython (Pico 2W / RP2350)
# Team 40: Portable CNC Machine
#
# Wiring (SPI0):
#   GP2  → Display RESET
#   GP3  → Display DC
#   GP5  → Display CS
#   GP6  → SPI0 SCK   (shared with touch T_CLK)
#   GP7  → SPI0 MOSI  (shared with touch T_DIN)
#   GP4  → SPI0 MISO  (touch T_DO only; display SDO unused)
#   GP8  → Display LED (backlight)
#
# SPI Mode 0 (CPOL=0, CPHA=0), 18-bit colour (3 bytes per pixel)
# MADCTL=0x28 → landscape 480×320, RGB order.
#   If colours look wrong  → try 0x68
#   If image is mirrored   → try 0x48

import machine, utime, framebuf

# ── Colour palette (R, G, B) ──────────────────────────────────────────────────
BLACK  = (  0,   0,   0)
WHITE  = (255, 255, 255)
RED    = (220,  30,  30)
GREEN  = ( 30, 180,  30)
BLUE   = ( 30, 100, 220)
YELLOW = (255, 210,   0)
ORANGE = (255, 140,   0)
GRAY   = ( 80,  80,  80)
DARK   = ( 20,  20,  20)
LGRAY  = (180, 180, 180)


class ILI9488:
    WIDTH  = 480
    HEIGHT = 320

    def __init__(self, spi, cs, dc, rst, bl=None):
        self.spi = spi
        self.cs  = cs
        self.dc  = dc
        self.rst = rst
        self.bl  = bl
        self.cs.value(1)
        self.dc.value(1)
        self._init_display()

    # ── Low-level SPI ─────────────────────────────────────────────────────────

    def _cmd(self, cmd):
        self.dc.value(0)
        self.cs.value(0)
        self.spi.write(bytes([cmd]))
        self.cs.value(1)

    def _data(self, data):
        self.dc.value(1)
        self.cs.value(0)
        if isinstance(data, (list, tuple)):
            self.spi.write(bytes(data))
        else:
            self.spi.write(bytes([data]))
        self.cs.value(1)

    # ── Initialisation sequence ───────────────────────────────────────────────

    def _init_display(self):
        self.rst.value(0); utime.sleep_ms(120)
        self.rst.value(1); utime.sleep_ms(120)

        self._cmd(0xE0)  # Positive Gamma Control
        self._data([0x00,0x03,0x09,0x08,0x16,0x0A,0x3F,0x78,0x4C,0x09,0x0A,0x08,0x16,0x1A,0x0F])

        self._cmd(0xE1)  # Negative Gamma Control
        self._data([0x00,0x16,0x19,0x03,0x0F,0x05,0x32,0x45,0x46,0x04,0x0E,0x0D,0x35,0x37,0x0F])

        self._cmd(0xC0); self._data([0x17, 0x15])    # Power Control 1
        self._cmd(0xC1); self._data([0x41])           # Power Control 2
        self._cmd(0xC5); self._data([0x00,0x12,0x80]) # VCOM Control

        self._cmd(0x36); self._data([0x28])           # MADCTL – landscape, RGB
        self._cmd(0x3A); self._data([0x66])           # COLMOD – 18-bit colour
        self._cmd(0xB0); self._data([0x00])           # Interface Mode Control
        self._cmd(0xB1); self._data([0xA0])           # Frame Rate Control (60 Hz)
        self._cmd(0xB4); self._data([0x02])           # Display Inversion Control
        self._cmd(0xB6); self._data([0x02,0x02,0x3B]) # Display Function Control
        self._cmd(0xB7); self._data([0xC6])           # Entry Mode Set
        self._cmd(0xF7); self._data([0xA9,0x51,0x2C,0x02]) # Adjust Control 3

        self._cmd(0x11); utime.sleep_ms(120)          # Sleep Out
        self._cmd(0x29)                               # Display On

        if self.bl:
            self.bl.value(1)

    # ── Drawing primitives ────────────────────────────────────────────────────

    def _set_window(self, x0, y0, x1, y1):
        """Set the active drawing window and send the RAMWR command."""
        self._cmd(0x2A)
        self._data([x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF])
        self._cmd(0x2B)
        self._data([y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF])
        self._cmd(0x2C)

    def fill_rect(self, x, y, w, h, color):
        """Fill a rectangle with a solid colour."""
        if w <= 0 or h <= 0:
            return
        x1 = min(x + w - 1, self.WIDTH  - 1)
        y1 = min(y + h - 1, self.HEIGHT - 1)
        self._set_window(x, y, x1, y1)
        r, g, b = color
        row = bytes([r, g, b] * (x1 - x + 1))
        self.dc.value(1)
        self.cs.value(0)
        for _ in range(y1 - y + 1):
            self.spi.write(row)
        self.cs.value(1)

    def fill(self, color):
        """Fill the entire screen with one colour."""
        self.fill_rect(0, 0, self.WIDTH, self.HEIGHT, color)

    def pixel(self, x, y, color):
        """Draw a single pixel."""
        if 0 <= x < self.WIDTH and 0 <= y < self.HEIGHT:
            self._set_window(x, y, x, y)
            r, g, b = color
            self.dc.value(1)
            self.cs.value(0)
            self.spi.write(bytes([r, g, b]))
            self.cs.value(1)

    def draw_rect(self, x, y, w, h, color):
        """Draw a hollow rectangle border."""
        self.fill_rect(x,         y,         w, 1, color)  # top
        self.fill_rect(x,         y + h - 1, w, 1, color)  # bottom
        self.fill_rect(x,         y,         1, h, color)  # left
        self.fill_rect(x + w - 1, y,         1, h, color)  # right

    def draw_hline(self, x, y, w, color):
        self.fill_rect(x, y, w, 1, color)

    def draw_vline(self, x, y, h, color):
        self.fill_rect(x, y, 1, h, color)

    # ── Text rendering ────────────────────────────────────────────────────────

    def draw_text(self, x, y, text, fg, bg=None, scale=1):
        """
        Render ASCII text using MicroPython's built-in 8×8 framebuf font.

        fg    : (R, G, B) foreground colour
        bg    : (R, G, B) background colour, or None for transparent
        scale : integer pixel multiplier
                  1 → 8 px tall (small)
                  2 → 16 px tall (readable)
                  3 → 24 px tall (large)
                  4 → 32 px tall (title / alert)
        """
        if not text:
            return

        cw  = len(text) * 8                    # source width in pixels
        buf = bytearray(((cw + 7) // 8) * 8)   # MONO_HLSB: ceil(cw/8) bytes × 8 rows
        tfb = framebuf.FrameBuffer(buf, cw, 8, framebuf.MONO_HLSB)
        tfb.text(text, 0, 0, 1)

        fr, fg_g, fg_b = fg
        fg_px = bytes([fr, fg_g, fg_b])

        if bg is not None:
            # Fast path: one window, continuous pixel stream
            br, bg_g, bb = bg
            bg_px = bytes([br, bg_g, bb])

            self._set_window(x, y, x + cw * scale - 1, y + 8 * scale - 1)
            self.dc.value(1)
            self.cs.value(0)

            for row in range(8):
                # Build one source row (cw pixels wide)
                row_buf = bytearray()
                for col in range(cw):
                    row_buf += fg_px if tfb.pixel(col, row) else bg_px

                # Scale horizontally
                if scale > 1:
                    scaled = bytearray()
                    for i in range(0, len(row_buf), 3):
                        scaled += row_buf[i:i + 3] * scale
                    row_buf = scaled

                # Write row, repeated for vertical scaling
                for _ in range(scale):
                    self.spi.write(row_buf)

            self.cs.value(1)

        else:
            # Transparent path: only draw foreground pixels individually
            for row in range(8):
                for col in range(cw):
                    if tfb.pixel(col, row):
                        if scale == 1:
                            self.pixel(x + col, y + row, fg)
                        else:
                            self.fill_rect(
                                x + col * scale, y + row * scale,
                                scale, scale, fg
                            )

    def text_width(self, text, scale=1):
        """Return the pixel width of a rendered string."""
        return len(text) * 8 * scale

    def text_height(self, scale=1):
        """Return the pixel height of a rendered string."""
        return 8 * scale
