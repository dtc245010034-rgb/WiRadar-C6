#!/bin/bash
# Biên dịch (và nạp) firmware bằng arduino-cli.
#   ./scripts/flash.sh                        biên dịch bản đầy đủ (WiRadar_C6: AP + web + radar)
#   ./scripts/flash.sh lite                   biên dịch bản Lite (WiRadar_C6_Lite: chỉ phần cứng, không web)
#   ./scripts/flash.sh [lite] upload [cổng]   biên dịch + nạp (đóng Serial Monitor trước)
# Cần arduino-cli (trong PATH, hoặc tools/arduino-cli/arduino-cli) và core esp32 3.x trở lên.
cd "$(dirname "$0")/.."
SKETCH=WiRadar_C6; UPLOAD=0; PORT=${WIRADAR_PORT:-/dev/ttyACM0}
for a in "$@"; do
  case "$a" in
    lite)   SKETCH=WiRadar_C6_Lite ;;
    upload) UPLOAD=1 ;;
    /dev/*|COM*) PORT="$a" ;;
  esac
done
if command -v arduino-cli >/dev/null 2>&1; then CLI=arduino-cli
elif [ -x ./tools/arduino-cli/arduino-cli ]; then CLI=./tools/arduino-cli/arduino-cli
else echo "Không tìm thấy arduino-cli (https://arduino.github.io/arduino-cli/)" >&2; exit 1; fi
FQBN=esp32:esp32:esp32c6:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB
BUILD=./build/$SKETCH
$CLI compile --fqbn "$FQBN" --build-path "$BUILD" "firmware/$SKETCH" || exit 1
[ "$UPLOAD" = 1 ] && $CLI upload -p "$PORT" --fqbn "$FQBN" --input-dir "$BUILD" "firmware/$SKETCH"
exit 0
