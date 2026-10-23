"""Log the AW1000 serial console (115200 baud) to a file until killed.

Usage: py serial_log.py <port> <logfile> [seconds]
"""
import sys
import time

import serial

port_name = sys.argv[1]
log_path = sys.argv[2]
duration = float(sys.argv[3]) if len(sys.argv) > 3 else 600

with serial.Serial(port_name, 115200, timeout=0.2) as port, open(log_path, "ab") as log:
    end = time.time() + duration
    while time.time() < end:
        chunk = port.read(4096)
        if chunk:
            log.write(chunk)
            log.flush()
