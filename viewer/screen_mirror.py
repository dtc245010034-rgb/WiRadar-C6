#!/usr/bin/env python3
"""
Screen mirror: shows on the laptop exactly what the board draws for its 1.44" TFT (128x128),
so the project can be demonstrated without a working display.

The board (WiRadar_C6 or WiRadar_C6_Lite) sends each frame over USB while it receives 'S' keep-alives.
The physical buttons on the board keep working; the on-screen buttons / keys send virtual commands.

Keys: Space = next screen | R = recalibrate | 4 / 6 = Wi-Fi mode | F = fullscreen | Esc = quit
"""
import argparse, base64, sys, threading, time

import numpy as np
import serial
from PyQt5.QtCore import Qt, QTimer, QRectF
from PyQt5.QtGui import QColor, QFont, QImage, QPainter, QPen
from PyQt5.QtWidgets import QApplication, QHBoxLayout, QPushButton, QVBoxLayout, QWidget

W = H = 128


def decode_frame(line):
    """'FRAME,<base64 of (count, hi, lo) runs>' -> HxW uint16 RGB565, or None if damaged."""
    try:
        raw = np.frombuffer(base64.b64decode(line[6:].strip(), validate=True), np.uint8)
    except ValueError:
        return None
    if len(raw) == 0 or len(raw) % 3:
        return None
    runs = raw.reshape(-1, 3).astype(np.uint16)
    if int(runs[:, 0].sum()) != W * H:
        return None
    return np.repeat((runs[:, 1] << 8) | runs[:, 2], runs[:, 0]).reshape(H, W).astype(np.uint16)


class Link:
    """Serial reader thread; keeps the newest frame and keeps the mirror switched on."""

    def __init__(self, port, baud=921600):
        self.port, self.baud = port, baud
        self.frame, self.times, self.error, self.ser = None, [], None, None
        self._stop = False
        self._lock = threading.Lock()
        threading.Thread(target=self._run, daemon=True).start()

    def send(self, data):
        with self._lock:
            try:
                if self.ser is not None and self.ser.is_open:
                    self.ser.write(data)
            except Exception:
                pass

    def stop(self):
        self.send(b's')
        self._stop = True

    def fps(self):
        now = time.time()
        return sum(1 for t in list(self.times) if now - t <= 1.0)

    def _run(self):
        while not self._stop:
            try:
                s = serial.Serial()
                s.port, s.baudrate, s.timeout = self.port, self.baud, 0.05
                s.dtr = False
                s.rts = False
                s.open()
            except Exception as e:
                self.error = f'Cannot open {self.port}. Close the Arduino Serial Monitor.\n{e}'
                time.sleep(1.0)
                continue
            self.ser, self.error = s, None
            buf, last_keep = b'', 0.0
            try:
                while not self._stop:
                    if time.time() - last_keep > 1.0:   # repeated: opening the port may reset the board
                        last_keep = time.time()
                        self.send(b'S')
                    buf += s.read(4096)
                    while b'\n' in buf:
                        line, buf = buf.split(b'\n', 1)
                        i = line.find(b'FRAME,')
                        if i >= 0:
                            f = decode_frame(line[i:])
                            if f is not None:
                                self.frame = f
                                self.times = self.times[-60:] + [time.time()]
                    if len(buf) > 60000:
                        buf = b''
            except Exception as e:
                self.error = f'Serial error: {e}'
                time.sleep(1.0)
            try:
                s.close()
            except Exception:
                pass


class Screen(QWidget):
    """Draws the module with its 128x128 picture, scaled with hard pixels."""

    def __init__(self, link):
        super().__init__()
        self.link = link
        self.setMinimumSize(420, 480)

    def paintEvent(self, _):
        p = QPainter(self)
        w, h = self.width(), self.height()
        side = min(w - 60, h - 110)
        side = max(W * 2, side // W * W)                  # whole-number scale keeps pixels sharp
        x, y = (w - side) // 2, (h - side) // 2
        p.setRenderHint(QPainter.Antialiasing)
        p.setPen(Qt.NoPen)
        p.setBrush(QColor(16, 52, 132))
        p.drawRoundedRect(QRectF(x - 24, y - 44, side + 48, side + 88), 14, 14)
        p.setBrush(QColor(0, 0, 0))
        p.drawRect(x - 8, y - 8, side + 16, side + 16)
        p.setPen(QColor(230, 235, 245))
        p.setFont(QFont('DejaVu Sans Mono', 9))
        p.drawText(QRectF(x, y - 38, side, 24), Qt.AlignCenter, 'GND VCC SCL SDA RES DC CS BLK')
        p.drawText(QRectF(x, y + side + 14, side, 24), Qt.AlignCenter, '1.44"  128x128  (mirrored from the board)')
        f = self.link.frame
        if f is not None:
            img = QImage(f.tobytes(), W, H, W * 2, QImage.Format_RGB16)
            p.setRenderHint(QPainter.SmoothPixmapTransform, False)
            p.drawImage(QRectF(x, y, side, side), img)
        stale = not self.link.times or time.time() - self.link.times[-1] > 2.5
        if f is None or stale:
            box = QRectF(x + 20, y + side / 2 - 46, side - 40, 92)
            p.setBrush(QColor(0, 0, 0, 220))
            p.setPen(QPen(QColor(255, 200, 0), 2))
            p.drawRoundedRect(box, 8, 8)
            p.setPen(QColor(255, 200, 0))
            p.setFont(QFont('DejaVu Sans', 11))
            p.drawText(box.adjusted(8, 6, -8, -6), Qt.AlignCenter | Qt.TextWordWrap,
                       self.link.error or 'Waiting for the board...\n(it may take ~20 s after a reset)')
        p.end()


class Window(QWidget):
    def __init__(self, link):
        super().__init__()
        self.link = link
        self.setWindowTitle('WiRadar-C6  -  TFT screen mirror')
        self.setStyleSheet('QWidget{background:#0d1117;color:#e6edf3}'
                           'QPushButton{background:#21262d;border:1px solid #3d444d;border-radius:8px;padding:10px 14px;font-size:14px}'
                           'QPushButton:pressed{background:#30363d}')
        self.screen = Screen(link)
        v = QVBoxLayout(self)
        v.addWidget(self.screen, 1)
        row = QHBoxLayout()
        for text, cmd in (('Next screen  (Space)', b'N'), ('Recalibrate  (R)', b'R'), ('Wi-Fi 4  (4)', b'4'), ('Wi-Fi 6  (6)', b'6')):
            b = QPushButton(text)
            b.setFocusPolicy(Qt.NoFocus)
            b.clicked.connect(lambda _, c=cmd: link.send(c))
            row.addWidget(b)
        v.addLayout(row)
        self.resize(720, 800)
        self.setFocusPolicy(Qt.StrongFocus)
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.screen.update)
        self.timer.start(40)

    def keyPressEvent(self, e):
        k = e.key()
        if k in (Qt.Key_Space, Qt.Key_N, Qt.Key_Right):
            if not e.isAutoRepeat():
                self.link.send(b'N')
        elif k == Qt.Key_R:
            self.link.send(b'R')
        elif k == Qt.Key_4:
            self.link.send(b'4')
        elif k == Qt.Key_6:
            self.link.send(b'6')
        elif k == Qt.Key_F:
            self.showNormal() if self.isFullScreen() else self.showFullScreen()
        elif k in (Qt.Key_Escape, Qt.Key_Q):
            self.close()

    def closeEvent(self, e):
        self.link.stop()
        super().closeEvent(e)


def main():
    ap = argparse.ArgumentParser(description='Mirror the WiRadar-C6 TFT screen on the laptop')
    ap.add_argument('-p', '--port', default='/dev/ttyACM0')
    ap.add_argument('--grab', help='(testing) save one screenshot per screen into this folder, then exit')
    a = ap.parse_args()
    link = Link(a.port)
    app = QApplication(sys.argv)
    win = Window(link)
    win.show()
    if a.grab:
        steps = {'n': 0}

        def snap():
            print(f"shot {steps['n']}: {link.fps()} frames/s", flush=True)
            win.grab().save(f"{a.grab}/screen{steps['n']}.png")
            steps['n'] += 1
            if steps['n'] >= 4:
                app.quit()
                return
            link.send(b'N')
            QTimer.singleShot(2500, snap)

        QTimer.singleShot(26000, snap)
    app.exec_()
    link.stop()


if __name__ == '__main__':
    main()
