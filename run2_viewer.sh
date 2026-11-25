#!/bin/bash
# Radar truc quan tren may tinh. Doc board qua USB (firmware WiRadar_C6 hoac WiRadar_C6_Lite).
# Dong Serial Monitor cua Arduino IDE truoc khi chay.
# Dung:  ./run2_viewer.sh [cong_serial] [--wifi 4|6]       Phim 4 / 6 trong cua so: doi che do Wi-Fi
cd "$(dirname "$0")"
mkdir -p data
PORT=/dev/ttyACM0
if [ -n "$1" ] && [ "${1#--}" = "$1" ]; then PORT="$1"; shift; fi
exec ./csi-venv/bin/python viewer/radar_viewer.py -p "$PORT" -r data/csi_radar_live.csv "$@"
