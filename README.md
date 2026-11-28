# WiRadar-C6

**Hệ thống phát hiện chuyển động không tiếp xúc dựa trên Wi-Fi CSI, dùng ESP32-C6**,
tích hợp đo nhiệt độ - độ ẩm (DHT11), hiển thị trên màn hình TFT và trang web xem bằng điện thoại.

*Contactless Wi-Fi CSI motion detection on ESP32-C6 with temperature/humidity monitoring, a TFT display and a phone web dashboard.*

> Tên đồ án (đề xuất):
> **Thiết kế hệ thống phát hiện chuyển động không tiếp xúc dựa trên Wi-Fi CSI sử dụng ESP32-C6, tích hợp giám sát nhiệt độ - độ ẩm và hiển thị qua web**
> *Design of a Contactless Wi-Fi CSI-Based Motion Detection System Using ESP32-C6 with Temperature/Humidity Monitoring and Web Dashboard*

## Nó làm được gì
- Đọc **CSI** (Channel State Information) từ các gói Wi-Fi giữa ESP32-C6 và router, tính **mức chuyển động** và báo khi có người đi qua / vẫy tay giữa hai thiết bị.
- **Cấu hình Wi-Fi bằng điện thoại:** lần đầu cắm điện, board phát Access Point `WiRadar-C6-Setup` (mật khẩu được **tạo ngẫu nhiên ở lần chạy đầu** và hiện trên màn hình TFT / Serial); điện thoại kết nối, chọn Wi-Fi nhà, nhập mật khẩu. Màn hình hiện tên Wi-Fi và địa chỉ IP.
- **Bảng điều khiển trên điện thoại** (`http://<IP>` hoặc `http://wiradar.local`): Radar, Đồ thị 30 giây, Nhiệt độ - độ ẩm, thông tin Wi-Fi.
- **Màn hình TFT ST7735 1.44"** với các màn: Mạng, Nhiệt độ - độ ẩm, Radar, Đồ thị.
- **Một nút bấm** (hành động khi thả nút): bấm ngắn = đổi màn hình; giữ 1-5 giây = hiệu chỉnh lại radar; giữ 5-10 giây = tạo lại Access Point; giữ từ **10 giây = đổi Wi-Fi 4 ⇄ Wi-Fi 6**.
- **Chế độ Wi-Fi 4 / Wi-Fi 6** và hiển thị chuẩn liên kết thật (802.11n hay 802.11ax) trên màn hình, trang web và cửa sổ trên máy tính; đổi được từ nút, web hoặc máy tính (`4` / `6`).
- Test được **chỉ với một board ESP32-C6** (không cần màn hình, DHT11, nút): dùng nút BOOT trên board và xem bằng điện thoại ở chế độ AP.

## Giới hạn (nói thẳng)
- Một board + một router chỉ cho biết **có chuyển động hay không và mạnh/yếu**, **không** biết vị trí, khoảng cách hay số lượng người. Hiệu ứng vòng sóng trên radar chỉ minh hoạ cường độ.
- Đứng yên chắn giữa hai thiết bị một lúc, mức nền tự thích nghi và radar quay về "trống".
- Chỉ băng 2.4 GHz. ESP32-C6 hỗ trợ Wi-Fi 6 (802.11ax) ở 2.4 GHz.

## Cấu trúc thư mục
| Thư mục | Nội dung |
|---|---|
| `firmware/WiRadar_C6/` | **Firmware đầy đủ** (Arduino): AP cấu hình, web điện thoại, TFT, nút bấm, radar, xuất dữ liệu qua USB |
| `firmware/WiRadar_C6_Lite/` | **Bản Lite, chỉ phần cứng** (không web, không AP): TFT + DHT11 + nút, nối thẳng vào router |
| `scripts/flash.sh` | Biên dịch / nạp firmware bằng arduino-cli |
| `scripts/sync_shared.py` | Chép các header dùng chung (`config.h`, `csi_core.h`, `wifi_mode.h`, `tft_ui.h`) từ bản đầy đủ sang bản Lite; sửa ở `firmware/WiRadar_C6/` rồi chạy lại |
| `scripts/radar.sh`, `raw.sh`, `mirror.sh` | Radar trên máy tính, đồ thị CSI thô, chiếu màn hình TFT |
| `viewer/` | Cửa sổ radar trên máy tính (`radar_viewer.py`) và công cụ vẽ CSI của Espressif |
| `demo/preview.html` | Xem trước giao diện màn hình 128x128 khi chưa có phần cứng |
| `case/` | Hộp in 3D **nằm ngang, lắp bằng khe** (`make_case.py`, STL, miếng thử, xem 3D `viewer.html`) |
| `docs/WIRING.md` | Sơ đồ nối dây |
| `docs/ARCHITECTURE.md` | **Sơ đồ hệ thống** (bản đầy đủ, bản Lite, thao tác nút) và các giao thức dùng |

## Nạp firmware (Arduino IDE hoặc arduino-cli)
1. Cài core **esp32 by Espressif Systems 3.x trở lên** (có hỗ trợ ESP32-C6).
2. Cài thư viện: *Adafruit GFX*, *Adafruit ST7735 and ST7789*, *DHT sensor library*, *Adafruit Unified Sensor*.
3. Mở `firmware/WiRadar_C6/WiRadar_C6.ino`. Chọn board **ESP32C6 Dev Module**, *Flash Size 16MB*, *USB CDC On Boot: Disabled*.
4. Upload (hoặc dùng `./scripts/flash.sh upload`; bản Lite: `./scripts/flash.sh lite upload`). Lần đầu board mở AP `WiRadar-C6-Setup` (xem Serial **921600** hoặc màn hình để biết IP). Mở cổng serial có thể làm board tự khởi động lại; đó là bình thường.

### Bản Lite (không web)
`WiRadar_C6_Lite` chạy y hệt phần radar / nhiệt độ - độ ẩm / đồ thị trên màn hình TFT nhưng **không có Access Point và trang web**. Nó dùng Wi-Fi đã lưu trên board (do bản đầy đủ cấu hình qua điện thoại); nếu chưa có thì copy `firmware/WiRadar_C6_Lite/secrets.h.example` thành `secrets.h` và điền Wi-Fi của bạn. Bấm ngắn đổi màn hình, giữ từ 1 giây để hiệu chỉnh lại.

## Radar trên máy tính
```bash
python3 -m venv csi-venv && ./csi-venv/bin/pip install -r viewer/requirements.txt
./scripts/radar.sh            # radar trực quan + chuẩn Wi-Fi; phím 4 / 6 đổi chế độ; đóng Serial Monitor trước
./scripts/raw.sh              # đồ thị CSI thô của Espressif (cần thêm: pip install -r viewer/requirements-espressif.txt)
./scripts/mirror.sh           # chiếu màn hình TFT của board lên máy tính (demo khi chưa có màn); Space = đổi màn
./scripts/radar.sh --wifi 6   # (radar.sh và raw.sh) yêu cầu board dùng Wi-Fi 4 hoặc Wi-Fi 6
# Cổng serial: đối số đầu tiên, hoặc biến môi trường WIRADAR_PORT (mặc định /dev/ttyACM0; Windows: COMx)
```

## Hộp in 3D
Xem `case/README.md`: hộp nằm ngang 112.7 × 42.4 × 36.4 mm, màn hình, DHT11, board và nút đều lắp bằng khe (chỉ 4 vít ngoài nắp). In các miếng thử `out/fit_*.stl` trước. Mở `case/viewer.html` để xem 3D.

## Ghi công
- `viewer/csi_data_read_parse.py`: từ [espressif/esp-csi](https://github.com/espressif/esp-csi) (Apache-2.0).
- Thuật toán và giao diện radar, firmware, hộp 3D: viết cho đồ án này.
