# methods.py – Application-level UI helpers for the Portable CNC Machine
# Team 40: Portable CNC Machine
# 2026/03/23
#
# Provides high-level routines used by main.py:
#   setup()                  – initialise SPI, display, touch, button
#   get_hardware()           – return (display, touch) objects
#   splash_screen(display)   – boot splash
#   show_state(state, ...)   – draw the correct screen for a given state
#   update_progress(display, pct) – redraw progress bar in-place (fast)
#   flash_estop(display)     – red-flash animation for emergency stop entry

import machine, utime
from ili9488 import ILI9488, BLACK, WHITE, RED, GREEN, DARK
from xpt2046 import XPT2046
from menu    import (build_idle_screen, build_estop_screen,
                     build_machining_screen, build_warning_screen,
                     build_transfer_screen, build_no_laptop_screen,
                     SW, SH, BODY_Y, BODY_H)

# ── Pin assignments ───────────────────────────────────────────────────────────
_SPI_ID   = 0
_SPI_SCK  = 6
_SPI_MOSI = 7
_SPI_MISO = 4

_DISP_RST = 2
_DISP_DC  = 3
_DISP_CS  = 5
_DISP_LED = 8

_TOUCH_CS  = 9
_TOUCH_IRQ = 10

_BUTTON_PIN = 15   # Physical emergency-stop button (active-low with PULL_UP)


def setup():
    """
    Initialise all hardware: SPI bus, display, touch controller, and button.

    Returns
    -------
    (display, touch, button) : ILI9488, XPT2046, machine.Pin
    """
    spi = machine.SPI(
        _SPI_ID,
        baudrate=40_000_000,
        polarity=0, phase=0,
        sck=machine.Pin(_SPI_SCK),
        mosi=machine.Pin(_SPI_MOSI),
        miso=machine.Pin(_SPI_MISO),
    )

    display = ILI9488(
        spi=spi,
        cs=machine.Pin(_DISP_CS,  machine.Pin.OUT),
        dc=machine.Pin(_DISP_DC,  machine.Pin.OUT),
        rst=machine.Pin(_DISP_RST, machine.Pin.OUT),
        bl=machine.Pin(_DISP_LED, machine.Pin.OUT),
    )

    touch = XPT2046(
        spi=spi,
        cs=machine.Pin(_TOUCH_CS,  machine.Pin.OUT),
        irq=machine.Pin(_TOUCH_IRQ, machine.Pin.IN, machine.Pin.PULL_UP),
    )

    button = machine.Pin(_BUTTON_PIN, machine.Pin.IN, machine.Pin.PULL_UP)

    return display, touch, button


def splash_screen(display):
    """Display a boot splash for ~2 seconds."""
    display.fill(DARK)

    title = "Portable CNC"
    sub   = "Team 40  –  Pico 2W"

    tw = display.text_width(title, 4)
    sw_ = display.text_width(sub,   2)

    display.draw_text((SW - tw) // 2,  90, title, GREEN, DARK, 4)
    display.draw_text((SW - sw_) // 2, 155, sub,   WHITE, DARK, 2)

    # Horizontal divider
    display.draw_hline(60, 140, SW - 120, GREEN)

    utime.sleep_ms(2000)


def show_state(display, state, progress_pct=0, warning_msg="Check tool & material"):
    """
    Render the full screen for the given state number.

    States
    ------
    1 – IDLE
    2 – EMERGENCY STOP
    3 – MACHINING
    4 – WARNING
    5 – DATA TRANSFER
    6 – NO LAPTOP

    Returns the Screen object (for hit-testing touch events).
    """
    if state == 1:
        screen = build_idle_screen(display)
    elif state == 2:
        screen = build_estop_screen(display)
    elif state == 3:
        screen = build_machining_screen(display, progress_pct)
    elif state == 4:
        screen = build_warning_screen(display, warning_msg)
    elif state == 5:
        screen = build_transfer_screen(display)
    elif state == 6:
        screen = build_no_laptop_screen(display)
    else:
        screen = build_idle_screen(display)

    screen.draw()
    return screen


def update_progress(display, pct):
    """
    Redraw only the progress bar area without rebuilding the whole screen.
    Call this inside the machining loop for smooth updates.
    pct : 0–100
    """
    pct = max(0, min(100, pct))
    bar_x, bar_y, bar_w, bar_h = 20, BODY_Y + 20, 440, 40

    # Clear bar interior then redraw
    display.fill_rect(bar_x + 2, bar_y + 2, bar_w - 4, bar_h - 4, BLACK)
    filled_w = (bar_w - 4) * pct // 100
    if filled_w > 0:
        display.fill_rect(bar_x + 2, bar_y + 2, filled_w, bar_h - 4, GREEN)
    unfilled_w = (bar_w - 4) - filled_w
    if unfilled_w > 0:
        display.fill_rect(bar_x + 2 + filled_w, bar_y + 2, unfilled_w, bar_h - 4,
                          (80, 80, 80))

    # Percentage label (erase old, draw new)
    display.fill_rect(0, BODY_Y + 75, SW, display.text_height(3), BLACK)
    label = str(pct) + "%"
    lw = display.text_width(label, 3)
    display.draw_text((SW - lw) // 2, BODY_Y + 75, label, WHITE, BLACK, 3)


def flash_estop(display):
    """Brief red-flash animation shown when E-STOP is triggered."""
    for _ in range(3):
        display.fill(RED)
        utime.sleep_ms(120)
        display.fill(DARK)
        utime.sleep_ms(80)
