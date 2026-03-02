#!/usr/bin/env bash
set -euo pipefail
# Serial monitor for M5Stack. Usage: ./scripts/serial_monitor.sh [timeout_seconds] [device]
# -hupcl prevents DTR toggle which would reset the ESP32
TIMEOUT=${1:-10}
DEVICE=${2:-/dev/ttyUSB0}
stty -F "$DEVICE" 115200 raw -echo -hupcl
timeout "$TIMEOUT" cat "$DEVICE"
