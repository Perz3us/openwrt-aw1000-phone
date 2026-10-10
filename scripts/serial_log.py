"""Log the AW1000 serial console (COM6, 115200) to a file until killed.

Usage: py serial_log.py <logfile> [seconds]
"""
import sys
import time

import serial

log_path = sys.argv[1]
duration = float(sys.argv[2]) if len(sys.argv) > 2 else 600

with serial.Serial("COM6", 115200, timeout=0.2) as port, open(log_path, "ab") as log:
    end = time.time() + duration
    while time.time() < end:
        chunk = port.read(4096)
        if chunk:
            log.write(chunk)
            log.flush()
