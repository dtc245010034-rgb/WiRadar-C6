#!/bin/bash
# Vẽ đồ thị CSI thô (công cụ của Espressif). Tự bật luồng CSI_DATA trên firmware WiRadar_C6.
# Đóng Serial Monitor của Arduino IDE trước khi chạy.
# Dùng:  ./scripts/raw.sh [cổng_serial] [--wifi 4|6]
#   --wifi 4   board chỉ dùng Wi-Fi 4 (11b/g/n)      --wifi 6   board cho phép Wi-Fi 6 (11ax)
cd "$(dirname "$0")/.."
mkdir -p data
PORT=${WIRADAR_PORT:-/dev/ttyACM0}
if [ -n "$1" ] && [ "${1#--}" = "$1" ]; then PORT="$1"; shift; fi
exec ./csi-venv/bin/python viewer/csi_raw_launcher.py -p "$PORT" -s data/csi_live.csv -l data/csi_log.txt "$@"
