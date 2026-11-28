#!/bin/bash
# Chiếu màn hình TFT của board lên máy tính (demo khi chưa có màn). Cần firmware WiRadar_C6 hoặc Lite.
# Đóng Serial Monitor của Arduino IDE trước khi chạy.
# Dùng:  ./scripts/mirror.sh [cổng_serial]
# Phím:  Space = đổi màn   R = hiệu chỉnh   4 / 6 = chế độ Wi-Fi   F = toàn màn hình   Esc = thoát
cd "$(dirname "$0")/.."
exec ./csi-venv/bin/python viewer/screen_mirror.py -p "${1:-${WIRADAR_PORT:-/dev/ttyACM0}}"
