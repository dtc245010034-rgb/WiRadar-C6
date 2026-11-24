#!/bin/bash
# Bien dich (va nap) firmware WiRadar_C6 bang arduino-cli.
#   ./scripts/flash.sh            chi bien dich
#   ./scripts/flash.sh upload     bien dich + nap len board (dong Serial Monitor truoc)
# Can arduino-cli (tools/arduino-cli/arduino-cli) va core esp32 3.x+ da cai.
cd "$(dirname "$0")/.."
CLI=./tools/arduino-cli/arduino-cli
FQBN=esp32:esp32:esp32c6:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB
BUILD=./build
$CLI compile --fqbn "$FQBN" --build-path "$BUILD" firmware/WiRadar_C6 || exit 1
if [ "$1" = "upload" ]; then
  $CLI upload -p "${2:-/dev/ttyACM0}" --fqbn "$FQBN" --input-dir "$BUILD" firmware/WiRadar_C6
fi
