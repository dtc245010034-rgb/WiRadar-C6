#!/bin/bash
# Ve do thi CSI thoi gian thuc. Dong Serial Monitor cua Arduino IDE truoc khi chay.
cd "$(dirname "$0")"
mkdir -p data
./csi-venv/bin/python viewer/csi_data_read_parse.py -p "${1:-/dev/ttyACM0}" -s data/csi_live.csv -l data/csi_log.txt
