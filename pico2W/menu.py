# menu.py – UI component library for the Portable CNC Machine touchscreen
# Team 40: Portable CNC Machine
#
# Provides:
#   Button     – touchable labelled rectangle
#   Screen     – manages a header + set of Buttons; handles hit-testing
#   Builders   – build_*() functions that return ready-made Screen objects
#                for each machine state
#
# All coordinates are in landscape 480×320 pixels.

from ili9488 import (
    ILI9488,
    BLACK, WHITE, RED, GREEN, BLUE,
    YELLOW, ORANGE, GRAY, DARK, LGRAY
)

# ── Layout constants ──────────────────────────────────────────────────────────
SW = 480          # screen width
SH = 320          # screen height
HDR_H  = 50       # header bar height
BODY_Y = HDR_H    # y-start of body area
BODY_H = SH - HDR_H  # body height (270 px)

# ── Additional colours ────────────────────────────────────────────────────────
TEAL    = ( 20, 160, 160)
MAROON  = (140,  20,  20)
DKGREEN = ( 20, 100,  20)
DKBLUE  = ( 20,  40, 120)


# ── Button ────────────────────────────────────────────────────────────────────

class Button:
    """
    A touchable, labelled rectangle.

    Parameters
    ----------
    x, y, w, h : position and size in pixels
    label      : text displayed on the button (keep short, ≤10 chars)
    color      : fill colour (R, G, B)
    text_color : label colour (R, G, B), defaults to WHITE
    scale      : text size multiplier (1–4)
    tag        : optional string identifier returned by Screen.handle_touch()
    """

    def __init__(self, x, y, w, h, label,
                 color, text_color=WHITE, scale=2, tag=None):
        self.x          = x
        self.y          = y
        self.w          = w
        self.h          = h
        self.label      = label
        self.color      = color
        self.text_color = text_color
        self.scale      = scale
        self.tag        = tag if tag is not None else label

    def draw(self, display):
        # Fill body
        display.fill_rect(self.x, self.y, self.w, self.h, self.color)
        # 2-pixel white border
        display.draw_rect(self.x, self.y, self.w, self.h, WHITE)
        display.draw_rect(self.x + 1, self.y + 1, self.w - 2, self.h - 2, WHITE)
        # Centred label
        tw = display.text_width(self.label, self.scale)
        th = display.text_height(self.scale)
        tx = self.x + (self.w - tw) // 2
        ty = self.y + (self.h - th) // 2
        display.draw_text(tx, ty, self.label, self.text_color, self.color, self.scale)

    def hit(self, x, y):
        """Return True if screen coordinate (x, y) falls inside this button."""
        return self.x <= x <= self.x + self.w and self.y <= y <= self.y + self.h


# ── Screen ────────────────────────────────────────────────────────────────────

class Screen:
    """
    Manages a header bar and a collection of Buttons.

    draw()         – render header + all buttons
    handle_touch() – return the tag of the first button hit, or None
    """

    def __init__(self, display, title,
                 title_color=WHITE,
                 header_color=DARK,
                 body_color=BLACK):
        self.display      = display
        self.title        = title
        self.title_color  = title_color
        self.header_color = header_color
        self.body_color   = body_color
        self.buttons      = []
        self._extra_draws = []   # list of callables: fn(display) for custom content

    def add_button(self, button):
        self.buttons.append(button)
        return button

    def add_draw_fn(self, fn):
        """Register a custom drawing function called after buttons are drawn."""
        self._extra_draws.append(fn)

    def draw(self):
        d = self.display
        # Header
        d.fill_rect(0, 0, SW, HDR_H, self.header_color)
        # Vertical accent line on left of header
        d.fill_rect(0, 0, 4, HDR_H, self.title_color)
        # Title text, centred vertically in header
        ty = (HDR_H - d.text_height(2)) // 2
        d.draw_text(12, ty, self.title, self.title_color, self.header_color, 2)
        # Body background
        d.fill_rect(0, BODY_Y, SW, BODY_H, self.body_color)
        # Buttons
        for btn in self.buttons:
            btn.draw(d)
        # Custom content
        for fn in self._extra_draws:
            fn(d)

    def handle_touch(self, x, y):
        """Return the tag of the button that was hit, or None."""
        for btn in self.buttons:
            if btn.hit(x, y):
                return btn.tag
        return None


# ── Screen builders ───────────────────────────────────────────────────────────
# Each returns a Screen pre-populated for a machine state.
# Button tags drive state transitions in main.py.

def build_idle_screen(display):
    """State 1 – IDLE: home menu with three action buttons."""
    s = Screen(display, "Portable CNC  |  IDLE",
               title_color=GREEN, header_color=DARK)

    btn_y = BODY_Y + 70
    btn_h = 110

    s.add_button(Button( 15, btn_y, 140, btn_h, "START",   DKGREEN, scale=2, tag="START_JOB"))
    s.add_button(Button(170, btn_y, 140, btn_h, "TRANSFER", DKBLUE,  scale=2, tag="TRANSFER"))
    s.add_button(Button(325, btn_y, 140, btn_h, "SETTINGS", GRAY,    scale=2, tag="SETTINGS"))

    return s


def build_estop_screen(display):
    """State 2 – EMERGENCY STOP: full-red alert with reset button."""
    s = Screen(display, "EMERGENCY STOP",
               title_color=WHITE, header_color=MAROON, body_color=RED)

    # Large centred STOP text
    def _draw_stop(d):
        msg = "!! STOPPED !!"
        tw = d.text_width(msg, 3)
        d.draw_text((SW - tw) // 2, BODY_Y + 50, msg, WHITE, RED, 3)
        hint = "Fix issue then press RESET"
        hw = d.text_width(hint, 1)
        d.draw_text((SW - hw) // 2, BODY_Y + 110, hint, WHITE, RED, 1)

    s.add_draw_fn(_draw_stop)

    # Single centred RESET button
    s.add_button(Button(165, BODY_Y + 160, 150, 70, "RESET", DARK, scale=2, tag="RESET"))

    return s


def build_machining_screen(display, progress_pct=0):
    """
    State 3 – MACHINING: progress bar + PAUSE and E-STOP buttons.
    progress_pct : 0–100
    """
    s = Screen(display, "Machining...",
               title_color=YELLOW, header_color=DARK)

    pct = max(0, min(100, progress_pct))

    def _draw_progress(d):
        # Progress bar outline
        bar_x, bar_y, bar_w, bar_h = 20, BODY_Y + 20, 440, 40
        d.draw_rect(bar_x, bar_y, bar_w, bar_h, WHITE)
        # Filled portion
        filled_w = (bar_w - 4) * pct // 100
        if filled_w > 0:
            d.fill_rect(bar_x + 2, bar_y + 2, filled_w, bar_h - 4, GREEN)
        # Unfilled portion (grey)
        unfilled_x = bar_x + 2 + filled_w
        unfilled_w = (bar_w - 4) - filled_w
        if unfilled_w > 0:
            d.fill_rect(unfilled_x, bar_y + 2, unfilled_w, bar_h - 4, GRAY)
        # Percentage label
        label = str(pct) + "%"
        lw = d.text_width(label, 3)
        d.draw_text((SW - lw) // 2, BODY_Y + 75, label, WHITE, BLACK, 3)

    s.add_draw_fn(_draw_progress)

    s.add_button(Button( 40, BODY_Y + 170, 160, 70, "PAUSE",  DKBLUE,  scale=2, tag="PAUSE"))
    s.add_button(Button(280, BODY_Y + 170, 160, 70, "E-STOP", MAROON,  scale=2, tag="ESTOP"))

    return s


def build_warning_screen(display, message="Check tool & material"):
    """State 4 – WARNING: amber alert with dismiss / E-STOP buttons."""
    s = Screen(display, "WARNING",
               title_color=BLACK, header_color=ORANGE, body_color=BLACK)

    def _draw_warning(d):
        # Warning symbol row
        sym = "  /!\\  "
        sw_ = d.text_width(sym, 4)
        d.draw_text((SW - sw_) // 2, BODY_Y + 20, sym, ORANGE, BLACK, 4)
        # Message
        mw = d.text_width(message, 1)
        d.draw_text((SW - mw) // 2, BODY_Y + 90, message, LGRAY, BLACK, 1)

    s.add_draw_fn(_draw_warning)

    s.add_button(Button( 40, BODY_Y + 170, 160, 70, "CONTINUE", DKGREEN, scale=2, tag="CONTINUE"))
    s.add_button(Button(280, BODY_Y + 170, 160, 70, "E-STOP",   MAROON,  scale=2, tag="ESTOP"))

    return s


def build_transfer_screen(display):
    """State 5 – DATA TRANSFER: status message + CANCEL button."""
    s = Screen(display, "Data Transfer",
               title_color=BLUE, header_color=DARK)

    def _draw_transfer(d):
        msg1 = "Receiving G-code"
        msg2 = "from laptop..."
        w1 = d.text_width(msg1, 2)
        w2 = d.text_width(msg2, 2)
        d.draw_text((SW - w1) // 2, BODY_Y + 50,  msg1, WHITE, BLACK, 2)
        d.draw_text((SW - w2) // 2, BODY_Y + 90,  msg2, LGRAY, BLACK, 2)

    s.add_draw_fn(_draw_transfer)

    s.add_button(Button(165, BODY_Y + 170, 150, 70, "CANCEL", MAROON, scale=2, tag="CANCEL"))

    return s


def build_no_laptop_screen(display):
    """State 6 – NO LAPTOP: connection error with retry / back buttons."""
    s = Screen(display, "Connection Error",
               title_color=ORANGE, header_color=DARK)

    def _draw_error(d):
        msg1 = "No laptop detected"
        msg2 = "Check USB connection"
        w1 = d.text_width(msg1, 2)
        w2 = d.text_width(msg2, 2)
        d.draw_text((SW - w1) // 2, BODY_Y + 50, msg1, WHITE,  BLACK, 2)
        d.draw_text((SW - w2) // 2, BODY_Y + 90, msg2, ORANGE, BLACK, 2)

    s.add_draw_fn(_draw_error)

    s.add_button(Button( 40, BODY_Y + 170, 160, 70, "RETRY", DKBLUE,  scale=2, tag="RETRY"))
    s.add_button(Button(280, BODY_Y + 170, 160, 70, "BACK",  GRAY,    scale=2, tag="BACK"))

    return s
