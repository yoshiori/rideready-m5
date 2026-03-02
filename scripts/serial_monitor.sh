#!/bin/bash
# Serial monitor for M5Stack (/dev/ttyUSB0)
# Usage: ./scripts/serial_monitor.sh [timeout_seconds]
TIMEOUT=${1:-10}
stty -F /dev/ttyUSB0 115200 raw -echo
timeout "$TIMEOUT" cat /dev/ttyUSB0
