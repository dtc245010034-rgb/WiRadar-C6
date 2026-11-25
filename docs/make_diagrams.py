#!/usr/bin/env python3
"""Generates the system diagrams (SVG + PNG) for the report.

    ../csi-venv/bin/python make_diagrams.py        (run from docs/)

Output: diagrams/system_full.svg|png, system_lite.svg|png, button_flow.svg|png, and diagrams.html
"""
import os
from html import escape

FONT = "DejaVu Sans, Noto Sans, Arial, sans-serif"
KINDS = {  # fill, stroke
    'ext':  ('#ffffff', '#57606a'),
    'wifi': ('#ddf4ff', '#0969da'),
    'proc': ('#dafbe1', '#1a7f37'),
    'net':  ('#fff8c5', '#9a6700'),
    'ui':   ('#ffeedb', '#bc4c00'),
    'ser':  ('#fbefff', '#8250df'),
    'gpio': ('#eaeef2', '#57606a'),
}


class Diagram:
    def __init__(self, w, h, title):
        self.w, self.h, self.items = w, h, []
        self.items.append(f'<rect width="{w}" height="{h}" fill="#ffffff"/>')
        self.text(24, 34, title, size=22, bold=True)

    def text(self, x, y, s, size=14, bold=False, color='#1f2328', anchor='start', italic=False):
        self.items.append(f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{size}" '
                          f'font-weight="{"700" if bold else "400"}" font-style="{"italic" if italic else "normal"}" '
                          f'fill="{color}" text-anchor="{anchor}">{escape(s)}</text>')

    def box(self, x, y, w, h, title, lines=(), kind='proc'):
        fill, stroke = KINDS[kind]
        self.items.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="10" fill="{fill}" stroke="{stroke}" stroke-width="2"/>')
        self.text(x + w / 2, y + 24, title, size=15, bold=True, anchor='middle')
        for i, ln in enumerate(lines):
            self.text(x + w / 2, y + 46 + i * 18, ln, size=12.5, color='#3d444d', anchor='middle')

    def container(self, x, y, w, h, title):
        self.items.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="16" fill="#f6f8fa" stroke="#8c959f" stroke-width="2" stroke-dasharray="8 6"/>')
        self.text(x + 16, y + 26, title, size=16, bold=True, color='#24292f')

    def _head(self, tip, frm, color, size=11):
        import math
        ang = math.atan2(tip[1] - frm[1], tip[0] - frm[0])
        p1 = (tip[0] - size * math.cos(ang - 0.42), tip[1] - size * math.sin(ang - 0.42))
        p2 = (tip[0] - size * math.cos(ang + 0.42), tip[1] - size * math.sin(ang + 0.42))
        self.items.append(f'<polygon points="{tip[0]:.1f},{tip[1]:.1f} {p1[0]:.1f},{p1[1]:.1f} {p2[0]:.1f},{p2[1]:.1f}" fill="{color}"/>')

    def arrow(self, pts, label=None, lpos=None, color='#57606a', both=False, dash=False, lw=2):
        d = 'M' + ' L'.join(f'{px},{py}' for px, py in pts)
        da = ' stroke-dasharray="6 5"' if dash else ''
        self.items.append(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{lw}"{da}/>')
        self._head(pts[-1], pts[-2], color)
        if both:
            self._head(pts[0], pts[1], color)
        if label:
            lx, ly = lpos if lpos else ((pts[0][0] + pts[-1][0]) / 2, (pts[0][1] + pts[-1][1]) / 2 - 6)
            wlab = len(label) * 7.0 + 12
            self.items.append(f'<rect x="{lx - wlab / 2}" y="{ly - 14}" width="{wlab}" height="20" rx="4" fill="#ffffff"/>')
            self.text(lx, ly, label, size=12, color=color, anchor='middle', italic=True)

    def legend(self, x, y, items):
        for i, (kind, name) in enumerate(items):
            fill, stroke = KINDS[kind]
            self.items.append(f'<rect x="{x}" y="{y + i * 24}" width="18" height="14" rx="3" fill="{fill}" stroke="{stroke}" stroke-width="1.5"/>')
            self.text(x + 26, y + 12 + i * 24, name, size=12.5, color='#3d444d')

    def save(self, base):
        svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" viewBox="0 0 {self.w} {self.h}">'
               f'{"".join(self.items)}</svg>')
        open(base + '.svg', 'w', encoding='utf-8').write(svg)
        return svg


LEG = [('ext', 'Linh kiện / thiết bị bên ngoài'), ('wifi', 'Lớp Wi-Fi (802.11, CSI)'), ('proc', 'Xử lý tín hiệu / trạng thái'),
       ('net', 'Mạng, cấu hình'), ('ui', 'Giao diện người dùng'), ('ser', 'Giao tiếp USB-serial')]


def system_full():
    d = Diagram(1340, 900, 'WiRadar-C6 — Sơ đồ hệ thống, bản đầy đủ (Access Point + web điện thoại)')
    d.box(30, 70, 200, 70, 'Nút bấm', ['GPIO2 (ngoài) + BOOT GPIO9'], 'ext')
    d.box(30, 170, 200, 70, 'Cảm biến DHT11', ['GPIO1 · 1-wire'], 'ext')
    d.box(30, 340, 200, 100, 'Router Wi-Fi nhà', ['chế độ STA: nguồn CSI', '(khung trả lời ping)'], 'ext')
    d.box(30, 480, 200, 100, 'Điện thoại (test AP)', ['chế độ AP: nguồn CSI', 'là chính điện thoại'], 'ext')
    d.container(270, 50, 795, 800, 'ESP32-C6 · firmware WiRadar_C6   (nguồn 5 V qua USB-C)')
    d.box(295, 90, 290, 110, 'Ngoại vi vào (GPIO)', ['đọc DHT11 mỗi 3 s', 'nút: ngắn = đổi màn · 1–5 s = hiệu chỉnh', '5–10 s = tạo lại AP · ≥10 s = Wi-Fi 4 ⇄ 6'], 'gpio')
    d.box(295, 245, 290, 70, 'Ping ICMP 100 gói/s', ['kích nguồn CSI trả lời liên tục'], 'wifi')
    d.box(295, 350, 290, 110, 'Wi-Fi 802.11 + CSI', ['STA (vào router) hoặc SoftAP', 'chế độ Wi-Fi 4 (11n) / Wi-Fi 6 (11ax)', 'callback CSI (cả khung HE): biên độ, RSSI'], 'wifi')
    d.box(295, 510, 290, 170, 'Xử lý CSI → mức chuyển động', ['chuẩn hoá biên độ mỗi gói', 'so với nền thích nghi chậm', 'làm mượt → mức chuyển động', 'hiệu chỉnh 5 s · ngưỡng + trễ', 'lịch sử 30 s'], 'proc')
    d.box(745, 90, 290, 110, 'Giao diện TFT (128×128)', ['4 màn: Mạng · Nhiệt độ/Ẩm', 'Radar · Đồ thị 30 s', 'vẽ vào bộ đệm rồi đẩy SPI'], 'ui')
    d.box(745, 350, 290, 130, 'Quản lý mạng', ['NVS lưu Wi-Fi nhà (SSID, mật khẩu)', 'AP "WiRadar-C6-Setup" (WPA2)', 'DHCP · DNS captive portal', 'mDNS: wiradar.local'], 'net')
    d.box(745, 520, 290, 140, 'Web server HTTP :80', ['/  bảng điều khiển · /setup  cấu hình Wi-Fi', '/api/state  (JSON 150 ms/lần)', '/api/scan · save · recal · forget', 'tab: Radar · Đồ thị · Nhiệt/Ẩm · Wi-Fi'], 'net')
    d.box(745, 700, 290, 100, 'Serial USB (UART 921600)', ['STATE: 10 dòng/s (chuyển động + chuẩn Wi-Fi)', 'CSI thô: "C" · đổi chế độ: "4" / "6"'], 'ser')
    d.box(1100, 95, 215, 100, 'Màn hình TFT ST7735', ['1.44" · SPI', 'SCK6 MOSI7 CS18 DC19 RST20'], 'ext')
    d.box(1100, 525, 215, 130, 'Điện thoại — trình duyệt', ['xem bảng điều khiển', 'cấu hình Wi-Fi nhà', 'cùng mạng hoặc vào AP'], 'ext')
    d.box(1100, 700, 215, 100, 'Máy tính', ['run2_viewer.sh (radar)', 'run_viewer.sh (CSI thô)'], 'ext')
    d.arrow([(230, 105), (295, 125)])
    d.arrow([(230, 205), (295, 160)])
    d.arrow([(230, 390), (262, 390), (262, 410), (295, 410)], 'Wi-Fi', (250, 378), '#0969da', both=True)
    d.arrow([(230, 530), (262, 530), (262, 435), (295, 435)], 'Wi-Fi', (250, 518), '#0969da', both=True)
    d.arrow([(440, 315), (440, 350)], color='#0969da')
    d.arrow([(440, 460), (440, 510)], 'CSI', (468, 490), '#1a7f37')
    d.arrow([(585, 140), (745, 140)], 'nút · DHT', (630, 128))
    d.arrow([(585, 415), (745, 415)], 'STA / AP', (635, 403), '#9a6700', both=True)
    d.arrow([(890, 480), (890, 520)], color='#9a6700')
    d.arrow([(585, 560), (665, 560), (665, 590), (745, 590)], 'ratio, level', (625, 548), '#1a7f37')
    d.arrow([(585, 610), (705, 610), (705, 160), (745, 160)], None, color='#1a7f37', dash=True)
    d.arrow([(585, 650), (680, 650), (680, 750), (745, 750)], 'STATE', (632, 638), '#8250df')
    d.arrow([(1035, 145), (1100, 145)], 'SPI', (1068, 133), '#bc4c00')
    d.arrow([(1035, 590), (1100, 590)], 'HTTP', (1068, 578), '#9a6700', both=True)
    d.arrow([(1035, 750), (1100, 750)], 'USB-C', (1068, 738), '#8250df')
    d.legend(30, 640, LEG)
    d.text(30, 880, 'Nét đứt: mức chuyển động cũng được vẽ lên màn TFT.', size=12, color='#57606a', italic=True)
    return d


def system_lite():
    d = Diagram(1310, 780, 'WiRadar-C6 Lite — Sơ đồ hệ thống, bản chỉ phần cứng (không web, không Access Point)')
    d.box(30, 70, 200, 70, 'Nút bấm', ['GPIO2 (ngoài) + BOOT GPIO9'], 'ext')
    d.box(30, 170, 200, 70, 'Cảm biến DHT11', ['GPIO1 · 1-wire'], 'ext')
    d.box(30, 340, 200, 100, 'Router Wi-Fi nhà', ['nguồn CSI', '(khung trả lời ping)'], 'ext')
    d.container(270, 50, 795, 660, 'ESP32-C6 · firmware WiRadar_C6_Lite   (nguồn 5 V qua USB-C)')
    d.box(295, 90, 290, 110, 'Ngoại vi vào (GPIO)', ['đọc DHT11 mỗi 3 s', 'nút: ngắn = đổi màn · 1–10 s = hiệu chỉnh', '≥ 10 s = đổi Wi-Fi 4 ⇄ 6'], 'gpio')
    d.box(295, 245, 290, 70, 'Ping ICMP 100 gói/s', ['kích router trả lời liên tục'], 'wifi')
    d.box(295, 350, 290, 110, 'Wi-Fi 802.11 + CSI', ['chỉ chế độ STA (vào router)', 'chế độ Wi-Fi 4 (11n) / Wi-Fi 6 (11ax)', 'callback CSI (cả khung HE): biên độ, RSSI'], 'wifi')
    d.box(295, 510, 290, 170, 'Xử lý CSI → mức chuyển động', ['chuẩn hoá biên độ mỗi gói', 'so với nền thích nghi chậm', 'làm mượt → mức chuyển động', 'hiệu chỉnh 5 s · ngưỡng + trễ', 'lịch sử 30 s'], 'proc')
    d.box(745, 90, 290, 110, 'Giao diện TFT (128×128)', ['4 màn: Mạng · Nhiệt độ/Ẩm', 'Radar · Đồ thị 30 s', 'vẽ vào bộ đệm rồi đẩy SPI'], 'ui')
    d.box(745, 350, 290, 130, 'Kết nối Wi-Fi (STA)', ['Wi-Fi lấy từ NVS (đã lưu)', 'hoặc secrets.h', 'DHCP · tự thử lại mỗi 20 s', 'không có AP, không web'], 'net')
    d.box(745, 570, 290, 110, 'Serial USB (UART 921600)', ['STATE: 10 dòng/s (chuyển động + chuẩn Wi-Fi)', 'CSI thô: "C" · đổi chế độ: "4" / "6"'], 'ser')
    d.box(1100, 95, 190, 100, 'Màn hình TFT ST7735', ['1.44" · SPI', 'SCK6 MOSI7 CS18', 'DC19 RST20'], 'ext')
    d.box(1100, 570, 190, 110, 'Máy tính', ['run2_viewer.sh (radar)', 'run_viewer.sh (CSI thô)'], 'ext')
    d.arrow([(230, 105), (295, 125)])
    d.arrow([(230, 205), (295, 160)])
    d.arrow([(230, 390), (262, 390), (262, 410), (295, 410)], 'Wi-Fi', (250, 378), '#0969da', both=True)
    d.arrow([(440, 315), (440, 350)], color='#0969da')
    d.arrow([(440, 460), (440, 510)], 'CSI', (468, 490), '#1a7f37')
    d.arrow([(585, 140), (745, 140)], 'nút · DHT', (630, 128))
    d.arrow([(585, 415), (745, 415)], 'STA', (635, 403), '#9a6700', both=True)
    d.arrow([(585, 600), (705, 600), (705, 170), (745, 170)], 'mức chuyển động', (640, 588), '#1a7f37')
    d.arrow([(585, 650), (745, 650)], 'STATE', (632, 638), '#8250df')
    d.arrow([(1035, 145), (1100, 145)], 'SPI', (1068, 133), '#bc4c00')
    d.arrow([(1035, 625), (1100, 625)], 'USB-C', (1068, 613), '#8250df')
    d.legend(30, 520, LEG)
    d.text(30, 745, 'Khác bản đầy đủ: bỏ Access Point, DNS, mDNS và web server; vẫn dùng chung phần xử lý CSI.', size=12, color='#57606a', italic=True)
    return d


def button_flow():
    d = Diagram(1280, 600, 'Thao tác nút bấm theo thời gian giữ (nút ngoài GPIO2 hoặc nút BOOT)')
    x0, x1, y = 70, 1210, 140                      # timeline 0..12 s
    def X(t): return x0 + (x1 - x0) * t / 12.0
    segs = [(0, 1, '#2da44e', 'Ngắn'), (1, 5, '#0969da', 'Giữ 1–5 s'), (5, 10, '#9a6700', 'Giữ 5–10 s'), (10, 12, '#8250df', 'Giữ ≥ 10 s')]
    d.text(70, 108, 'Màn hình hiện thanh tiến trình khi giữ quá 1 s; hành động xảy ra khi THẢ NÚT.', size=13, color='#3d444d')
    for a_, b_, col, name in segs:
        d.items.append(f'<rect x="{X(a_)}" y="{y}" width="{X(b_) - X(a_)}" height="34" fill="{col}" fill-opacity="0.9"/>')
        d.text((X(a_) + X(b_)) / 2, y + 22, name, size=14, bold=True, color='#ffffff', anchor='middle')
    for t in (0, 1, 5, 10):
        d.items.append(f'<line x1="{X(t)}" y1="{y + 34}" x2="{X(t)}" y2="{y + 48}" stroke="#57606a" stroke-width="2"/>')
        d.text(X(t), y + 66, f'{t} s', size=13, anchor='middle', color='#3d444d')
    d.text(x1, y + 66, 'thời gian giữ nút →', size=12, anchor='end', color='#57606a', italic=True)

    cols = [70, 360, 650, 940]; w = 270
    centers = [(X(0) + X(1)) / 2, (X(1) + X(5)) / 2, (X(5) + X(10)) / 2, (X(10) + X(12)) / 2]
    colors = ['#2da44e', '#0969da', '#9a6700', '#8250df']
    for cx, bx, colr in zip(centers, cols, colors):            # connectors timeline -> first row
        d.arrow([(cx, y + 36), (cx, 226), (bx + w / 2, 226), (bx + w / 2, 276)], color=colr, lw=1.6)
    d.text(228, 262, 'Bản đầy đủ (WiRadar_C6)', size=16, bold=True)
    full = [('Đổi màn hình', ['Mạng → Nhiệt/Ẩm → Radar', '→ Đồ thị → Mạng'], 'proc'),
            ('Hiệu chỉnh lại radar', ['giữ khu vực yên tĩnh 5 s', 'xoá lịch sử, tính lại mức nền'], 'proc'),
            ('Tạo lại Access Point', ['"WiRadar-C6-Setup"', 'điện thoại chọn Wi-Fi nhà'], 'net'),
            ('Đổi chuẩn Wi-Fi 4 ⇄ 6', ['chỉ board radar đổi (11n ⇄ 11ax)', 'tự kết nối lại + hiệu chỉnh'], 'wifi')]
    for bx, (t, ls, k) in zip(cols, full):
        d.box(bx, 280, w, 80, t, ls, k)
    d.text(70, 412, 'Bản Lite (WiRadar_C6_Lite)', size=16, bold=True)
    lite = [('Đổi màn hình', ['Nhiệt/Ẩm → Radar → Đồ thị', '→ Mạng → Nhiệt/Ẩm'], 'proc'),
            ('Hiệu chỉnh lại radar', ['giữ khu vực yên tĩnh 5 s', 'xoá lịch sử, tính lại mức nền'], 'proc'),
            ('(không có hành động)', ['bản Lite không có Access Point', 'thả nút: như giữ 1–5 s'], 'gpio'),
            ('Đổi chuẩn Wi-Fi 4 ⇄ 6', ['chỉ board radar đổi (11n ⇄ 11ax)', 'tự kết nối lại + hiệu chỉnh'], 'wifi')]
    for bx, (t, ls, k) in zip(cols, lite):
        d.box(bx, 430, w, 80, t, ls, k)
    d.text(70, 566, 'Router / AP không bị đổi: liên kết chỉ lên Wi-Fi 6 khi router cũng hỗ trợ 802.11ax ở 2.4 GHz.', size=12.5, color='#57606a', italic=True)
    d.text(70, 585, 'Chuẩn đang dùng (đã thương lượng) hiện trên màn hình, trang web và cửa sổ trên máy tính.', size=12.5, color='#57606a', italic=True)
    return d


if __name__ == '__main__':
    os.makedirs('diagrams', exist_ok=True)
    pages = []
    for name, fn in (('system_full', system_full), ('system_lite', system_lite), ('button_flow', button_flow)):
        d = fn()
        svg = d.save(f'diagrams/{name}')
        pages.append((name, svg))
    # PNG via Qt (2x for the report)
    os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
    from PyQt5.QtGui import QGuiApplication, QImage, QPainter, QColor
    from PyQt5.QtSvg import QSvgRenderer
    from PyQt5.QtCore import QByteArray
    app = QGuiApplication([])
    for name, svg in pages:
        r = QSvgRenderer(QByteArray(svg.encode('utf-8')))
        sz = r.defaultSize()
        img = QImage(sz.width() * 2, sz.height() * 2, QImage.Format_ARGB32)
        img.fill(QColor('white'))
        p = QPainter(img)
        r.render(p)
        p.end()
        img.save(f'diagrams/{name}.png')
    body = ''.join(f'<section><img src="diagrams/{n}.svg" alt="{n}"></section>' for n, _ in pages)
    open('diagrams.html', 'w', encoding='utf-8').write(
        '<!doctype html><html lang="vi"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
        '<title>WiRadar-C6 diagrams</title><style>body{margin:0;background:#eef0f2;font-family:system-ui}'
        'section{background:#fff;margin:16px auto;max-width:1300px;padding:8px;border-radius:8px;box-shadow:0 1px 4px #0003}'
        'img{width:100%;height:auto;display:block}</style><body>' + body + '</body></html>')
    print('done:', [n for n, _ in pages])
