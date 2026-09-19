#!/bin/bash
# Flash everything build.sh produced (bootloader, partition table, app, WHD) with the host esptool.
# Docker on macOS cannot reach USB, so this runs on the host. Usage: ./flash.sh [port]
cd "$(dirname "$0")"
PORT="${1:-$(ls /dev/cu.usbmodem* /dev/ttyACM* 2>/dev/null | head -1)}"
ESPTOOL=$(command -v esptool || command -v esptool.py)
cd build_docker && "$ESPTOOL" --chip esp32c6 --port "$PORT" --baud 921600 write_flash @flash_args
