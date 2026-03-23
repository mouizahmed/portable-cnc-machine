# main.py – Portable CNC Machine controller (Pico 2W)
# Team 40: Portable CNC Machine
# 2026/03/23
#
# State machine driven by ILI9488 touchscreen + physical E-STOP button.
#
# States
# ──────
#   1  IDLE          – home screen, awaiting user input
#   2  EMERGENCY STOP – locked until operator presses RESET on screen
#   3  MACHINING     – job in progress; shows live progress bar
#   4  WARNING       – operator must acknowledge before continuing
#   5  DATA TRANSFER – receiving G-code from laptop over USB
#   6  NO LAPTOP     – transfer attempted with no laptop detected
#
# Touch transitions
# ─────────────────
#   IDLE       : [START]    → MACHINING
#                [TRANSFER] → DATA TRANSFER
#   E-STOP     : [RESET]    → IDLE
#   MACHINING  : [PAUSE]    → IDLE
#                [E-STOP]   → E-STOP
#   WARNING    : [CONTINUE] → MACHINING
#                [E-STOP]   → E-STOP
#   TRANSFER   : [CANCEL]   → IDLE
#                (auto)     → NO LAPTOP if USB not detected
#   NO LAPTOP  : [RETRY]    → DATA TRANSFER
#                [BACK]     → IDLE
#
# Physical button (GP15, active-low) always → E-STOP regardless of state.

import utime
from methods import setup, splash_screen, show_state, update_progress, flash_estop

# ── Constants ─────────────────────────────────────────────────────────────────
STATE_IDLE      = 1
STATE_ESTOP     = 2
STATE_MACHINING = 3
STATE_WARNING   = 4
STATE_TRANSFER  = 5
STATE_NO_LAPTOP = 6

TOUCH_DEBOUNCE_MS  = 300   # minimum ms between touch events
TOUCH_POLL_MS      = 50    # how often to poll for touch / button
PROGRESS_STEP_MS   = 2000  # ms between simulated progress increments (demo)

# ── Initialise hardware ───────────────────────────────────────────────────────
display, touch, estop_btn = setup()
splash_screen(display)

# ── State machine variables ───────────────────────────────────────────────────
state         = STATE_IDLE
prev_state    = -1          # force full redraw on first loop
current_screen = None       # Screen object for hit-testing

progress_pct   = 0          # machining progress 0–100
last_progress_t = utime.ticks_ms()

last_touch_t   = 0          # ticks_ms of last accepted touch event
warning_msg    = "Check tool & material"


def trigger_estop():
    """Immediately enter E-STOP state with flash animation."""
    global state, prev_state
    flash_estop(display)
    state = STATE_ESTOP
    prev_state = -1   # force redraw


# ── E-STOP button interrupt (physical button, active-low) ────────────────────
def _btn_irq(pin):
    # IRQ fires on falling edge (button pressed, pulls GP15 to GND)
    trigger_estop()

estop_btn.irq(_btn_irq, trigger=estop_btn.IRQ_FALLING)


# ── Main loop ─────────────────────────────────────────────────────────────────
while True:
    now = utime.ticks_ms()

    # ── Redraw screen when state changes ─────────────────────────────────────
    if state != prev_state:
        current_screen = show_state(display, state,
                                    progress_pct=progress_pct,
                                    warning_msg=warning_msg)
        prev_state = state

    # ── Touch polling ─────────────────────────────────────────────────────────
    touch_pos = touch.get_touch()

    if touch_pos is not None:
        elapsed = utime.ticks_diff(now, last_touch_t)

        if elapsed >= TOUCH_DEBOUNCE_MS:
            last_touch_t = now
            tx, ty = touch_pos
            tag = current_screen.handle_touch(tx, ty) if current_screen else None

            if tag:
                # ── State transitions ─────────────────────────────────────────
                if state == STATE_IDLE:
                    if tag == "START_JOB":
                        progress_pct = 0
                        state = STATE_MACHINING
                    elif tag == "TRANSFER":
                        state = STATE_TRANSFER

                elif state == STATE_ESTOP:
                    if tag == "RESET":
                        state = STATE_IDLE

                elif state == STATE_MACHINING:
                    if tag == "PAUSE":
                        state = STATE_IDLE
                    elif tag == "ESTOP":
                        trigger_estop()

                elif state == STATE_WARNING:
                    if tag == "CONTINUE":
                        state = STATE_MACHINING
                    elif tag == "ESTOP":
                        trigger_estop()

                elif state == STATE_TRANSFER:
                    if tag == "CANCEL":
                        state = STATE_IDLE

                elif state == STATE_NO_LAPTOP:
                    if tag == "RETRY":
                        state = STATE_TRANSFER
                    elif tag == "BACK":
                        state = STATE_IDLE

    # ── Machining: simulate progress & auto-complete ───────────────────────────
    if state == STATE_MACHINING:
        if utime.ticks_diff(now, last_progress_t) >= PROGRESS_STEP_MS:
            last_progress_t = now
            progress_pct += 5

            if progress_pct >= 100:
                progress_pct = 100
                update_progress(display, progress_pct)
                utime.sleep_ms(1000)
                state = STATE_IDLE           # job complete → return to IDLE
            else:
                update_progress(display, progress_pct)

    # ── Data Transfer: simulate laptop detection (replace with real USB check) ─
    if state == STATE_TRANSFER:
        # TODO: replace with actual USB/serial presence detection
        # For now, auto-advance to NO_LAPTOP after 3 s to demonstrate the flow
        # (In production, check machine.USB or a UART handshake here.)
        utime.sleep_ms(3000)
        state = STATE_NO_LAPTOP

    utime.sleep_ms(TOUCH_POLL_MS)
