#!/bin/bash
# Chieu man hinh TFT cua board len may tinh (demo khi chua co man hinh). Can firmware WiRadar_C6 hoac Lite.
# Dong Serial Monitor cua Arduino IDE truoc khi chay.
# Dung:  ./run3_viewer.sh [cong_serial]
# Phim:  Space = doi man   R = hieu chinh   4 / 6 = che do Wi-Fi   F = toan man hinh   Esc = thoat
cd "$(dirname "$0")"
exec ./csi-venv/bin/python viewer/screen_mirror.py -p "${1:-/dev/ttyACM0}"
