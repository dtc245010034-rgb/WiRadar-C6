#!/bin/bash
# Bien dich (va nap) firmware bang arduino-cli.
#   ./scripts/flash.sh                 bien dich ban day du (WiRadar_C6: AP + web + radar)
#   ./scripts/flash.sh lite            bien dich ban Lite (WiRadar_C6_Lite: chi phan cung, khong web)
#   ./scripts/flash.sh [lite] upload [cong]   bien dich + nap (dong Serial Monitor truoc)
# Can arduino-cli (tools/arduino-cli/arduino-cli) va core esp32 3.x+ da cai.
cd "$(dirname "$0")/.."
SKETCH=WiRadar_C6; UPLOAD=0; PORT=/dev/ttyACM0
for a in "$@"; do
  case "$a" in
    lite)   SKETCH=WiRadar_C6_Lite ;;
    upload) UPLOAD=1 ;;
    /dev/*) PORT="$a" ;;
  esac
done
CLI=./tools/arduino-cli/arduino-cli
FQBN=esp32:esp32:esp32c6:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB
BUILD=./build/$SKETCH
$CLI compile --fqbn "$FQBN" --build-path "$BUILD" "firmware/$SKETCH" || exit 1
[ "$UPLOAD" = 1 ] && $CLI upload -p "$PORT" --fqbn "$FQBN" --input-dir "$BUILD" "firmware/$SKETCH"
exit 0
