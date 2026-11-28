# Kiểm thử

## Tự động (CI, GitHub Actions: `.github/workflows/ci.yml`)
| Job | Kiểm tra gì |
|---|---|
| `firmware (WiRadar_C6)`, `firmware (WiRadar_C6_Lite)` | Biên dịch cả hai sketch bằng arduino-cli (core esp32, ESP32-C6, flash 16 MB) |
| `consistency` | Các header dùng chung ở bản Lite khớp bản đầy đủ (`python3 scripts/sync_shared.py --check`) |
| `host-tests` | Test phần toán CSI trên máy tính (`tests/test_csi_math.cpp`) |
| `python` | Các script Python biên dịch được |

Chạy test toán CSI ở máy bạn:
```bash
g++ -O2 -std=c++17 -Wall -Wextra -o /tmp/test_csi_math tests/test_csi_math.cpp && /tmp/test_csi_math
```
Test so sánh đường tính **số nguyên** (chạy trên board) với đường tính **float** gốc trên khung CSI **tổng hợp**
(kênh tĩnh + nhiễu + vật chuyển động), kiểm tra: sàn nhiễu lệch dưới 10 %, mọi quyết định phát hiện trùng nhau,
các trường hợp biên (gói toàn 0, -128, một sóng mang quá lớn), trung vị và giới hạn trôi của sàn.

> Dữ liệu tổng hợp chỉ chứng minh hai cách tính cho kết quả như nhau. **Nó không thay cho đo thật trên board.**

## Việc cần làm trên phần cứng (chưa làm được trong CI)
1. Nạp firmware, để board ở nơi trống 5 giây hiệu chỉnh, kiểm tra radar hiện "Area clear".
2. Đi ngang giữa board và router: phải báo `MOTION`; đứng yên một lúc: về "clear".
3. So `pps` (gói/giây) và `ratio` trước/sau thay đổi này (xem dòng `STATE` hoặc màn Đồ thị).
4. Thu bộ dữ liệu có nhãn để chỉnh ngưỡng: `./scripts/radar.sh -r data/lan1.csv` (ghi CSI_DATA thô; bật luồng thô bằng
   `./scripts/raw.sh`), ghi lại giờ bắt đầu/kết thúc mỗi lần đi qua, rồi chạy lại thuật toán trên file đó.
5. Khi thêm BLE hoặc Zigbee: ghi `pps` và độ nhiễu của `floorMotion` khi tắt/bật từng giao thức (5 phút mỗi trường hợp).
