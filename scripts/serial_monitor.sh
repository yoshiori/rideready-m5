#!/bin/bash
# Serial monitor for M5Stack (/dev/ttyUSB0)
# Usage: ./scripts/serial_monitor.sh [timeout_seconds]
# -hupcl prevents DTR toggle which would reset the ESP32
TIMEOUT=${1:-10}
stty -F /dev/ttyUSB0 115200 raw -echo -hupcl
timeout "$TIMEOUT" cat /dev/ttyUSB0
