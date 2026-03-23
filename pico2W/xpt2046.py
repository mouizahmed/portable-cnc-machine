# xpt2046.py – XPT2046 resistive touch controller driver for MicroPython (Pico 2W)
# Team 40: Portable CNC Machine
#
# The XPT2046 shares SPI0 with the ILI9488 display (separate CS pins).
# This driver temporarily lowers the SPI baudrate to 2 MHz for each touch
# read, then restores it to 40 MHz for the display.
#
# Wiring (shared SPI0):
#   GP6  → T_CLK  (shared with display SCK)
#   GP7  → T_DIN  (shared with display MOSI)
#   GP4  → T_DO   (MISO; display SDO not connected)
#   GP9  → T_CS
#   GP10 → T_IRQ  (active-low; interrupt or polled)
#
# Calibration:
#   cal_x0 / cal_x1 : raw ADC values at left / right screen edges
#   cal_y0 / cal_y1 : raw ADC values at top  / bottom screen edges
#   These defaults are approximate; run a calibration routine on first use.

import utime

# XPT2046 command bytes (differential reference mode, 12-bit)
_CMD_X  = 0xD0   # Measure X position
_CMD_Y  = 0x90   # Measure Y position
_CMD_Z1 = 0xB0   # Measure Z1 (pressure, for touch detection without IRQ)
_CMD_Z2 = 0xC0   # Measure Z2

_TOUCH_BAUDRATE   = 2_000_000   # XPT2046 max SPI clock
_DISPLAY_BAUDRATE = 40_000_000  # Restored after touch read


class XPT2046:
    def __init__(self, spi, cs, irq=None,
                 cal_x0=300,  cal_x1=3700,
                 cal_y0=300,  cal_y1=3700,
                 screen_w=480, screen_h=320):
        """
        spi       : shared machine.SPI instance (SPI0)
        cs        : machine.Pin for T_CS (active-low)
        irq       : machine.Pin for T_IRQ (active-low), or None to use pressure
        cal_*     : raw ADC calibration values at screen edges (tune per unit)
        screen_w/h: display resolution in pixels
        """
        self.spi      = spi
        self.cs       = cs
        self.irq      = irq
        self.cal_x0   = cal_x0
        self.cal_x1   = cal_x1
        self.cal_y0   = cal_y0
        self.cal_y1   = cal_y1
        self.screen_w = screen_w
        self.screen_h = screen_h
        self.cs.value(1)

    # ── Low-level SPI ─────────────────────────────────────────────────────────

    def _read_raw(self, cmd):
        """
        Send one XPT2046 command byte and read back the 12-bit ADC result.

        Frame (SPI Mode 0):
          Host → [S A2 A1 A0 MODE SER/DFR PD1 PD0]   (8 bits)
          Chip → [0 D11 D10 … D0 0 0 0]              (16 bits)
          Result = (byte0 << 8 | byte1) >> 3  (top 12 bits, leading 0 stripped)
        """
        self.cs.value(0)
        self.spi.write(bytes([cmd]))
        data = self.spi.read(2)
        self.cs.value(1)
        return (((data[0] << 8) | data[1]) >> 3) & 0xFFF

    def _sample(self, cmd, n=5):
        """Average n raw ADC samples to reduce noise."""
        total = 0
        for _ in range(n):
            total += self._read_raw(cmd)
        return total // n

    # ── Public interface ──────────────────────────────────────────────────────

    def is_touched(self):
        """
        Returns True if the screen is currently being pressed.
        Uses T_IRQ (active-low) when available; falls back to Z1 pressure.
        """
        if self.irq is not None:
            return not self.irq.value()

        # Fallback: check Z1 pressure (higher = more pressure)
        self.spi.init(baudrate=_TOUCH_BAUDRATE, polarity=0, phase=0)
        z1 = self._read_raw(_CMD_Z1)
        self.spi.init(baudrate=_DISPLAY_BAUDRATE, polarity=0, phase=0)
        return z1 > 200

    def get_touch(self):
        """
        Returns (x, y) screen coordinates if the screen is touched, else None.

        Note on orientation: if X/Y axes appear swapped or inverted after
        mounting, swap cal_x↔cal_y assignments in get_touch() or adjust the
        MADCTL register in ili9488.py.
        """
        if not self.is_touched():
            return None

        # Switch to touch-safe baudrate
        self.spi.init(baudrate=_TOUCH_BAUDRATE, polarity=0, phase=0)

        rx = self._sample(_CMD_X)
        ry = self._sample(_CMD_Y)

        # Restore display baudrate
        self.spi.init(baudrate=_DISPLAY_BAUDRATE, polarity=0, phase=0)

        # Verify still touched (debounce: reject lifted-finger noise)
        if not self.is_touched():
            return None

        # Map raw ADC → screen pixels (linear interpolation, clamped)
        x = (rx - self.cal_x0) * self.screen_w // max(1, self.cal_x1 - self.cal_x0)
        y = (ry - self.cal_y0) * self.screen_h // max(1, self.cal_y1 - self.cal_y0)

        x = max(0, min(self.screen_w - 1, x))
        y = max(0, min(self.screen_h - 1, y))

        return (x, y)

    def get_raw(self):
        """
        Return raw (rx, ry) ADC values — useful for calibration.
        Prints values over serial; press corners and note the numbers.
        """
        self.spi.init(baudrate=_TOUCH_BAUDRATE, polarity=0, phase=0)
        rx = self._sample(_CMD_X, n=10)
        ry = self._sample(_CMD_Y, n=10)
        self.spi.init(baudrate=_DISPLAY_BAUDRATE, polarity=0, phase=0)
        return (rx, ry)

    def set_calibration(self, x0, x1, y0, y1):
        """
        Update calibration values at runtime.
        Touch top-left corner → note raw values → set as (x0, y0).
        Touch bottom-right corner → note raw values → set as (x1, y1).
        """
        self.cal_x0 = x0
        self.cal_x1 = x1
        self.cal_y0 = y0
        self.cal_y1 = y1
