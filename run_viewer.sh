#!/bin/bash
# Ve do thi CSI tho (cong cu cua Espressif). Tu bat luong CSI_DATA tren firmware WiRadar_C6.
# Dong Serial Monitor cua Arduino IDE truoc khi chay.
# Dung:  ./run_viewer.sh [cong_serial] [--wifi 4|6]
#   --wifi 4   board chi dung Wi-Fi 4 (11b/g/n)      --wifi 6   board cho phep Wi-Fi 6 (11ax)
cd "$(dirname "$0")"
mkdir -p data
PORT=/dev/ttyACM0
if [ -n "$1" ] && [ "${1#--}" = "$1" ]; then PORT="$1"; shift; fi
exec ./csi-venv/bin/python viewer/csi_raw_launcher.py -p "$PORT" -s data/csi_live.csv -l data/csi_log.txt "$@"
