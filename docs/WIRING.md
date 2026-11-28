# Đấu nối: ESP32-C6 + TFT ST7735 1.44" (128×128) + DHT11 + nút bấm

Chân GPIO đổi được ở đầu file `WiRadar_C6.ino` (mục "Pins").
Kiểm tra nhãn chân (silkscreen) trên board của bạn trước khi nối.

## Màn hình TFT ST7735 1.44" (SPI)
| Chân TFT | Nối tới ESP32-C6 |
|---|---|
| VDD / VCC | 3V3 |
| GND | GND |
| SCL (SCK) | GPIO6 |
| SDA (MOSI) | GPIO7 |
| CS | GPIO18 |
| DC (A0) | GPIO19 |
| RES (RST) | GPIO20 |
| BLK (đèn nền) | 3V3 (sáng liên tục) |

## DHT11 (module 3 chân)
| Chân | Nối tới |
|---|---|
| VCC / + | 3V3 |
| GND / - | GND |
| DATA / S / OUT | GPIO1 |

## Nút bấm
Một chân nút → GPIO2, chân còn lại → GND (dùng điện trở kéo lên nội của chip).

## Tránh các chân này
- GPIO16, GPIO17: đang dùng cho cổng USB-UART (nạp code / Serial).
- GPIO8, GPIO9, GPIO15: chân khởi động (strapping). GPIO9 là nút BOOT trên board.
- GPIO12, GPIO13: USB native.

## Cách dùng (firmware WiRadar_C6; bản Lite giống nhưng không có Access Point / web)
- Bấm nút ngắn: đổi màn hình theo vòng **Mạng (Wi-Fi/IP) → Nhiệt độ/Độ ẩm → Radar → Đồ thị → Mạng …**
- Giữ nút 1-5 giây rồi thả: **hiệu chỉnh lại** radar.
- Giữ nút 5-10 giây rồi thả (bản đầy đủ): tạo lại Access Point `WiRadar-C6-Setup`.
- Giữ nút **10 giây trở lên** rồi thả: **đổi Wi-Fi 4 ⇄ Wi-Fi 6** (board tự kết nối lại).
- Khi chưa gắn nút ngoài, dùng **nút BOOT (GPIO9)** trên board để thử.

## Nếu màn hình sai màu / lệch / có viền nhiễu
Sketch đang dùng `INITR_144GREENTAB` (đúng cho màn 1.44" thông thường).
Nếu vẫn lệch hoặc sai màu, đổi `TFT_TAB` thành `INITR_BLACKTAB` hoặc `INITR_REDTAB`.

## Xem trước giao diện khi chưa có phần cứng
Mở `demo/preview.html` bằng trình duyệt, hoặc chạy `./scripts/mirror.sh` khi đã có board.

## Khắc phục sự cố
| Hiện tượng | Cách xử lý |
|---|---|
| Không mở được cổng serial / "busy" | Đóng Serial Monitor của Arduino IDE; chỉ một chương trình dùng cổng tại một thời điểm |
| Mở cổng làm board khởi động lại | Bình thường với USB-UART; các script `scripts/*.sh` đã mở cổng với DTR/RTS thấp |
| Màn hình trắng / không hiện | Kiểm tra chân CS/DC/RST, BLK nối 3V3; thử đổi `TFT_TAB` |
| "NO CSI DATA" trên radar | Chưa nhận gói từ router (STA) hoặc chưa có điện thoại nối vào AP; xem mục "Mạng" trên màn hình |
| Báo chuyển động liên tục | Hiệu chỉnh lại (giữ nút 1-5 s) khi khu vực trống; đặt board cố định, tránh vật chuyển động gần ăng-ten |
| DHT11 hiện `--` | Kiểm tra chân DATA nối GPIO1 và nguồn 3V3 |
