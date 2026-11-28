# Hộp WiRadar-C6 nằm ngang (lắp bằng khe, không dán)

Kích thước ngoài: **112.7 × 42.4 × 36.4 mm** (rộng × cao × sâu). Xem 3D: mở `case/viewer.html`.
Sinh lại: sửa số trong `PARAMS` của `make_case.py` rồi chạy `../csi-venv/bin/python make_case.py` (trong thư mục `case/`; cần `pip install -r ../viewer/requirements-tools.txt`).

Nhìn vào mặt trước (mặt có màn hình):
```
[ màn hình ] | [ DHT11 ] | [ board ESP32-C6 → cổng USB ]        nút bấm: trên nóc, phía trên board
```

## Linh kiện và kích thước dùng
| Linh kiện | Kích thước dùng | Nguồn |
|---|---|---|
| Board ESP32-C6 N16 (2 USB-C) | 46.2 × 28 mm | thông số người bán |
| Màn TFT 1.44" 128×128 | PCB ≈ 29.0 × 39.3 × 4.2 mm, cửa sổ 27 mm | **ước lượng từ ảnh** (quy chiếu theo bước chân 2.54 mm) |
| DHT11 (module 3 chân) | 32 × 14 mm, cảm biến cao ≈ 7 mm | hình trong tài liệu |
| Nút bấm 12×12 mm (có nắp tròn) | thân 12 × 12 × 4.3, nắp Ø≈7 | ảnh |

**Màn hình và nút là số ước lượng, hãy in thử các miếng thử nhỏ trước** (xem bên dưới) rồi đo lại bằng thước cặp nếu không vừa.

## File in
- `out/body.stl`: thân hộp. In **úp mặt trước xuống bàn in**.
- `out/cover.stl`: nắp sau (đã lật sẵn). In như xuất ra.
- `out/fit_display.stl`, `fit_dht11.stl`, `fit_button.stl`: **miếng thử** (vài phút mỗi miếng) để kiểm tra khe màn hình, khe DHT11 và đường trượt nút trước khi in cả hộp.
- PLA/PETG, lớp 0.2 mm, 3 vách, không cần support. Vít: 4 × M2 × 8 mm tự ren (vít ngoài vỏ duy nhất).

## Cách lắp (không keo)
1. **Màn hình:** trượt vào khe nhỏ ở **thành trái**, mặt kính hướng ra trước, chân header (8 chân) về phía bên phải. Hai gờ trên/dưới giữ mép, tường chắn bên phải làm điểm dừng.
2. **DHT11:** đặt vào ngăn giữa từ phía sau, cảm biến hướng ra mặt trước (có khe thông gió), đầu chân 3 chân ở phía dưới (có chỗ hở cho dây).
3. **Board ESP32-C6:** đặt vào ngăn bên phải từ phía sau, mặt linh kiện hướng ra trước, **hai cổng USB-C hướng về thành phải** (có một khe rộng cho cả hai cổng). Các gờ đỡ và gờ định vị giữ board.
4. **Nút bấm:** trượt vào đường rãnh dưới nóc (chui từ phía sau), nắp nhô qua lỗ trên nóc. Cần **hàn dây vào chân nút** (không cắm dupont vào chân tact được).
5. Nối dây (xem `docs/WIRING.md`), rồi đóng **nắp sau**. Các gờ trên nắp ép nhẹ vào board, DHT11 và nút nên chúng không bị rơi hay xê dịch. Vặn 4 vít M2.

## Màn hình TFT
Module nằm nghiêng nên firmware xoay hình bằng `TFT_ROTATION` (mặc định **3**, chân header bên phải). Nếu hình bị xoay sai chiều, đổi thành `1` (hoặc `2`). Giao diện vẫn 128×128 (màn vuông) nên không phải vẽ lại.
Xem trước bằng `demo/preview.html` (tick "Landscape case").

## Chưa kiểm chứng (cần in thử)
- Kích thước màn hình, vị trí cửa sổ và độ chặt khe.
- Vị trí chân header của màn hình và cổng USB trên board thật (khe USB rộng 26 mm cho cả hai cổng).
- Vùng dây phía sau sâu 22 mm để vừa đầu cắm dupont; nếu hàn dây trực tiếp có thể giảm `WIRE_ZONE` xuống khoảng 10 để hộp mỏng hơn.
