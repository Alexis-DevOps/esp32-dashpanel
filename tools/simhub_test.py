#!/usr/bin/env python3
"""Send fake SimHub lines to the panel (needs pyserial: pip install pyserial).
Usage: simhub_test.py /dev/ttyACM0
"""
import math, sys, time
import serial

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
with serial.Serial(port, 115200, dsrdtr=False, rtscts=False) as ser:
    ser.dtr = ser.rts = False
    t0 = time.time()
    while True:
        t = time.time() - t0
        rpm = int(3500 + 3000 * math.sin(t))
        speed = int(100 + 90 * math.sin(t))
        gear = max(1, min(6, 1 + speed // 35))
        fuel = int(100 - (t % 100))
        ser.write(f"R:{rpm},S:{speed},G:{gear},F:{fuel},M:8000,L:6500\n".encode())
        time.sleep(1 / 30)
