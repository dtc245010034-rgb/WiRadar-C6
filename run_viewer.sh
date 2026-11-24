#!/bin/bash
# Ve do thi CSI tho (cong cu cua Espressif). Tu bat luong CSI_DATA tren firmware WiRadar_C6.
# Dong Serial Monitor cua Arduino IDE truoc khi chay.
# Dung:  ./run_viewer.sh [cong_serial]
cd "$(dirname "$0")"
mkdir -p data
exec ./csi-venv/bin/python viewer/csi_raw_launcher.py -p "${1:-/dev/ttyACM0}" -s data/csi_live.csv -l data/csi_log.txt
