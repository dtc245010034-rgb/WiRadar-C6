# Day noi - ESP32-C6 + TFT ST7735 1.44" (128x128) + DHT11 + nut bam

Chan GPIO co the doi o dau file `WiRadar_C6.ino` (muc "Chan ket noi").
Kiem tra nhan chan (silkscreen) tren board cua ban truoc khi noi.

## Man hinh TFT ST7735 1.44" (SPI)
| Chan TFT | Noi toi ESP32-C6 |
|---|---|
| VDD / VCC | 3V3 |
| GND | GND |
| SCL (SCK) | GPIO6 |
| SDA (MOSI) | GPIO7 |
| CS | GPIO18 |
| DC (A0) | GPIO19 |
| RES (RST) | GPIO20 |
| BLK (den nen) | 3V3 (sang lien tuc) |

## DHT11 (module 3 chan)
| Chan | Noi toi |
|---|---|
| VCC / + | 3V3 |
| GND / - | GND |
| DATA / S / OUT | GPIO1 |

## Nut bam
Mot chan nut -> GPIO2, chan con lai -> GND. (Dung dien tro keo len noi san cua chip.)

## Tranh cac chan nay
- GPIO16, GPIO17: dang dung cho cong USB-UART (nap code / Serial).
- GPIO8, GPIO9, GPIO15: chan khoi dong (strapping). GPIO9 la nut BOOT tren board.
- GPIO12, GPIO13: USB native.

## Cach dung (firmware WiRadar_C6; ban Lite giong nhung khong co Access Point/web)
- Bam nut ngan: doi man hinh theo vong **Mang (Wi-Fi/IP) -> Nhiet do/Do am -> Radar -> Do thi -> Mang ...**
- Giu nut 1-5 giay roi tha: **hieu chinh lai** radar.
- Giu nut 5-10 giay roi tha (ban day du): tao lai Access Point `WiRadar-C6-Setup`.
- Giu nut **10 giay tro len** roi tha: **doi Wi-Fi 4 <-> Wi-Fi 6** (board tu ket noi lai).
- Khi chua gan nut ngoai, dung **nut BOOT (GPIO9)** tren board de thu.

## Neu man hinh sai mau / lech / co vien nhieu
Sketch dang dung `INITR_144GREENTAB` (dung cho man 1.44" thong thuong).
Neu van lech hoac sai mau, doi `TFT_TAB` thanh `INITR_BLACKTAB` hoac `INITR_REDTAB`.

## Xem truoc giao dien khi chua co phan cung
Mo file `~/esp32/demo/preview.html` bang trinh duyet.
