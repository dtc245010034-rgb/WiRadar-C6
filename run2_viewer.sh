#!/bin/bash
# Radar truc quan tren may tinh (doc CSI qua USB tu board chay csi_recv_router.ino).
# Dong Serial Monitor cua Arduino IDE truoc khi chay.
# Dung:  ./run2_viewer.sh [cong_serial]      Vi du: ./run2_viewer.sh /dev/ttyACM0
cd "$(dirname "$0")"
mkdir -p data
exec ./csi-venv/bin/python viewer/radar_viewer.py -p "${1:-/dev/ttyACM0}" -r data/csi_radar_live.csv
