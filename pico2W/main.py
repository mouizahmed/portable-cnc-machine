# main.py by Farzin Aliverdi
# 2026/02/29
# This file handles the main flow and control system of the Pico2W 

from methods import *
from machine import Pin

#### VARIABLES ####
count = 0
display = ""
button = Pin(15, Pin.IN, Pin.PULL_DOWN)

#### ALL METHODS ####
def init():
    setupLCD()
    displayString(1,0,"Power On")
    displayString(2,0,"")
    longDelay(1000)
    clearScreen()
    displayString(1,0,"Team 40: Portable")
    displayString(2,0,"CNC Machine")
    longDelay(1000) 
    clearScreen()
    button.irq(button_handler)

def button_handler(pin):
    global count
    print(count)
    count += 1

#### INTERRUPTS ####
button.irq(button_handler, Pin.IRQ_FALLING)

init()
while(True):
    display = "Count: " + str(count)
    displayString(1,0,"--------")
    displayString(2,0, display)
    #clearScreen()
    print("MAIN: " + str(count))
    #longDelay(500)
    
    #count += 1
