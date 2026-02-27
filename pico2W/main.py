# methods.py by Farzin Aliverdi
# 2026/02/29
# This file handles the main flow and control system of the Pico2W 

from methods import *
from machine import Pin

#### VARIABLES ####
count = 0
state = 0
display1 = ""
display2 = ""
button = Pin(15, Pin.IN, Pin.PULL_DOWN)

#### ALL METHODS ####
def init():
    setupLCD()
    displayString(1,0,"Power On")
    displayString(2,0,"")
    longDelay(1000)
    clearScreen()
    displayString(1,0,"Team 40:Portable")
    displayString(2,0,"CNC Machine")
    longDelay(1000) 
    clearScreen()
    state = 1
    button.irq(button_handler)

def button_handler(pin):
    global count
    print(count)
    count += 1
    
def forceStopAll():
    pass

#### INTERRUPTS ####
button.irq(button_handler, Pin.IRQ_FALLING)

init()
while(True):
    display1 = "State no." + str(state)
    if (state == 1): # IDLE
        display2 = "IDLE"
    elif (state == 2): # EMERGENCY STOP
        display2 = "EMERGENCY STOP"
        forceStopAll()
    elif (state == 3): # MACHINING
        display2 = "MACHINING 10%"
    elif (state == 4): # WARNING
        display2 = "WARNING"
    elif (state == 5):
        display2 = "DATA TRANSFER"
    elif (state == 6):
        display2 = "NO LAPTOP FOUND"
    
    clearScreen()
    displayString(1,0, display1)
    displayString(2,0, display2)
    
    longDelay(1000)
    if (state == 6):
        state = 0
    state += 1
