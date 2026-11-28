#!/bin/bash
# Radar trực quan trên máy tính. Đọc board qua USB (firmware WiRadar_C6 hoặc WiRadar_C6_Lite).
# Đóng Serial Monitor của Arduino IDE trước khi chạy.
# Dùng:  ./scripts/radar.sh [cổng_serial] [--wifi 4|6]       Phím 4 / 6 trong cửa sổ: đổi chế độ Wi-Fi
cd "$(dirname "$0")/.."
mkdir -p data
PORT=${WIRADAR_PORT:-/dev/ttyACM0}
if [ -n "$1" ] && [ "${1#--}" = "$1" ]; then PORT="$1"; shift; fi
exec ./csi-venv/bin/python viewer/radar_viewer.py -p "$PORT" -r data/csi_radar_live.csv "$@"
