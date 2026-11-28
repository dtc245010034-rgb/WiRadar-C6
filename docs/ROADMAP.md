# Lộ trình: BLE và Zigbee trên ESP32-C6

ESP32-C6 có Wi-Fi 6, BLE 5 và 802.15.4 (Zigbee/Thread) nhưng **dùng chung một bộ thu phát 2.4 GHz**. Radar CSI cần
luồng gói đều (100 gói/giây), nên mỗi giao thức thêm vào có thể làm `pps` dao động và radar nhiễu hơn.
**Quy tắc: đo `pps` và `floorMotion` trước khi quyết định chạy đồng thời.**

## Giai đoạn 1: BLE (rủi ro thấp)
- Cấu hình Wi-Fi qua BLE (NimBLE) thay cho AP + captive portal.
- GATT notify: mức chuyển động, nhiệt độ, độ ẩm, chuẩn Wi-Fi, xem bằng Web Bluetooth hoặc ứng dụng GATT.
- Tuỳ chọn: quét RSSI của điện thoại để kết hợp với CSI.

## Giai đoạn 2: Zigbee (cần kiểm chứng)
- WiRadar là **Router** (cắm điện liên tục), cần một **Coordinator** riêng: ESP32-H2 (rẻ, không có Wi-Fi nên không ảnh hưởng CSI)
  hoặc ESP32-C6 thứ hai, hoặc dongle Zigbee + Home Assistant.
- Cluster đề xuất: Occupancy Sensing (0x0406), Temperature (0x0402), Relative Humidity (0x0405).
- **Chưa kiểm chứng:** thư viện Zigbee của Arduino-ESP32 chạy đồng thời với Wi-Fi + CSI. Thử bằng sketch riêng trước;
  nếu không ổn thì dùng chế độ luân phiên (đổi bằng nút).

## Điều kiện trước khi bắt đầu
- Cấu trúc đã tách module (`csi_core.h`, `wifi_mode.h`, `tft_ui.h`, `climate.h`); giao thức mới thêm thành module riêng
  (`ble_service.h`, `zigbee_node.h`) và thêm vào `scripts/sync_shared.py` nếu bản Lite cũng dùng.
- Phân vùng flash: `scripts/flash.sh` dùng `app3M_fat9M_16MB` (app 3 MB). Zigbee có thể cần phân vùng riêng.
- Đo cơ sở (chưa có BLE/Zigbee) ghi ở `docs/TESTING.md`, mục "Việc cần làm trên phần cứng".
