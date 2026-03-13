# main.py by Team 40
# Safety Supervisor: state machine, LCD display, button, main loop

from methods import *
from machine import Pin, UART, WDT
import utime
import protocol

# ── Hardware ──────────────────────────────────────────────────────────────────
teensy = UART(0, baudrate=115200, tx=Pin(0), rx=Pin(1))
button = Pin(15, Pin.IN, Pin.PULL_DOWN)

# ── State machine ─────────────────────────────────────────────────────────────
# 1=IDLE  2=EMERGENCY STOP  3=MACHINING  4=WARNING  5=DATA TRANSFER  6=NO LAPTOP
state = 6

# ── LCD refresh ───────────────────────────────────────────────────────────────
_last_lcd_ms   = 0
LCD_PERIOD_MS  = 500
_last_display1 = ""
_last_display2 = ""

def button_handler(pin):
    pass  # reserved for future use

def force_stop():
    teensy.write(b'\x18')   # GRBL Ctrl-X = immediate stop

def update_lcd():
    global _last_display1, _last_display2
    d1 = "State no." + str(state)
    if   state == 1: d2 = "IDLE"
    elif state == 2: d2 = "EMERGENCY STOP"
    elif state == 3: d2 = "MACHINING"
    elif state == 4: d2 = "WARNING"
    elif state == 5: d2 = "DATA TRANSFER"
    else:            d2 = "NO LAPTOP FOUND"

    if d1 != _last_display1 or d2 != _last_display2:
        clearScreen()
        displayString(1, 0, d1)
        displayString(2, 0, d2)
        _last_display1 = d1
        _last_display2 = d2

# ── Initialise ────────────────────────────────────────────────────────────────
button.irq(button_handler, Pin.IRQ_FALLING)
protocol.init(teensy)

setupLCD()
displayString(1, 0, "Power On")
displayString(2, 0, "")
longDelay(1000)
clearScreen()
displayString(1, 0, "Team 40:Portable")
displayString(2, 0, "CNC Machine")
longDelay(1000)
clearScreen()

utime.sleep_ms(200)

# Watchdog — resets Pico if loop freezes (e.g. blocked stdout on USB disconnect)
wdt = WDT(timeout=8000)

# ── Main loop ─────────────────────────────────────────────────────────────────
while True:
    wdt.feed()
    now = utime.ticks_ms()

    state = protocol.poll(state)

    if state == 2:
        force_stop()

    if utime.ticks_diff(now, _last_lcd_ms) >= LCD_PERIOD_MS:
        update_lcd()
        _last_lcd_ms = now

    utime.sleep_us(200)
