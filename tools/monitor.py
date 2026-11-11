"""Read the CYD's serial output for N seconds (default 30), finding the CH340 port automatically.

Usage: py -3.12 tools/monitor.py [seconds]
"""
import sys
import time

import serial
from serial.tools import list_ports

CH340_VID = 0x1A86

ports = [p.device for p in list_ports.comports() if p.vid == CH340_VID]
if not ports:
    sys.exit("No CH340 (CYD) serial port found - is the board plugged in?")

seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 30
print(f"[monitor] {ports[0]} for {seconds:.0f}s", flush=True)
with serial.Serial(ports[0], 115200, timeout=1) as s:
    s.dtr = False
    s.rts = False
    end = time.time() + seconds
    while time.time() < end:
        line = s.readline()
        if line:
            print(line.decode(errors="replace").rstrip(), flush=True)
