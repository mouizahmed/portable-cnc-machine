# protocol.py by Team 40
# USB CDC (GUI) <-> UART (Teensy/GRBL) relay protocol

import sys
import select
from machine import unique_id
import ubinascii

FIRMWARE  = "0.1.0"
SERIAL_NO = ubinascii.hexlify(unique_id()).decode()

# GRBL real-time bytes are forwarded immediately without buffering
REALTIME = frozenset([ord('?'), ord('!'), ord('~'), 0x18])  # ?, !, ~, Ctrl-X

_teensy     = None
_gui_buf    = bytearray()
_teensy_buf = bytearray()
_usb_poll   = None
_poll_tick  = 0
_POLL_REFRESH = 500   # recreate poll object every ~100 ms (500 × 200 µs)

def init(teensy_uart):
    global _teensy, _usb_poll
    _teensy   = teensy_uart
    _usb_poll = _make_poll()

def _make_poll():
    p = select.poll()
    p.register(sys.stdin, select.POLLIN)
    return p

def _parse_grbl(line, state):
    """Return updated state based on a GRBL line from the Teensy."""
    if line.startswith('<') and line.endswith('>'):
        grbl = line[1:-1].split('|')[0].split(':')[0]
        return {'Idle': 1, 'Run': 3, 'Jog': 3, 'Home': 3,
                'Hold': 5, 'Alarm': 4, 'Door': 4}.get(grbl, state)
    if line.startswith('ALARM:'):
        return 4
    if line.startswith('ok') or line.startswith('Grbl'):
        return 1 if state == 6 else state
    return state

def poll(state):
    """
    Process one relay iteration.
    Forwards data between USB CDC (GUI) and UART (Teensy).
    Returns the (possibly updated) state.
    """
    global _gui_buf, _teensy_buf, _usb_poll, _poll_tick

    # Periodically recreate the poll object so it stays fresh after
    # USB CDC host disconnect / reconnect without a physical replug.
    _poll_tick += 1
    if _poll_tick >= _POLL_REFRESH:
        _poll_tick = 0
        _usb_poll  = _make_poll()

    # ── GUI → Teensy ──────────────────────────────────────────────────────────
    events = _usb_poll.poll(0)
    if events:
        ev = events[0][1]
        if ev & (select.POLLHUP | select.POLLERR):
            _gui_buf  = bytearray()
            _usb_poll = _make_poll()
            state = 6
        elif ev & select.POLLIN:
            try:
                ch = sys.stdin.read(1)
            except OSError:
                ch = None
            if ch:
                b = ord(ch)
                if b in REALTIME:
                    if b == ord('?'):
                        try:
                            sys.stdout.write("[PICO:" + FIRMWARE + "]\n")
                            sys.stdout.write("[SN:"   + SERIAL_NO + "]\n")
                        except OSError:
                            pass
                    _teensy.write(ch)
                elif ch in ('\n', '\r'):
                    if _gui_buf:
                        _teensy.write(bytes(_gui_buf) + b'\n')
                        _gui_buf = bytearray()
                else:
                    _gui_buf.extend(ch.encode('ascii', 'ignore'))

    # ── Teensy → GUI ──────────────────────────────────────────────────────────
    while _teensy.any():
        data = _teensy.read(1)
        if data:
            b = data[0]
            if b == 0x0A:           # newline — flush buffered line to GUI
                if _teensy_buf:
                    line = _teensy_buf.decode('ascii', 'ignore')
                    try:
                        sys.stdout.write(line + '\n')
                    except OSError:
                        pass
                    state       = _parse_grbl(line, state)
                    _teensy_buf = bytearray()
            elif b != 0x0D:         # skip carriage return
                _teensy_buf.extend(data)

    return state
