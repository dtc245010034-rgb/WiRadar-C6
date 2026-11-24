#!/usr/bin/env python3
"""
Wi-Fi CSI radar viewer (PC side).

Reads the ESP32-C6 over USB serial. Two firmwares are understood (baud is auto-detected):
  * WiRadar_C6.ino      -> "STATE,..." lines at 921600 (motion level already computed on the board)
  * raw "CSI_DATA,..." lines at 921600 (motion level computed here)
and draws the motion level as a radar:
  - green rings + sweeping beam = idle, red rings + expanding ripples + central glow = motion
  - bar, status text and a scrolling history graph on the right.

The motion algorithm is the same as the one running on the board (WiRadar_C6.ino):
  per packet: amplitude of each subcarrier, normalised by its mean (removes AGC gain),
  compared with a slowly adapting baseline, then smoothed.

Keys: R = recalibrate | F = fullscreen | Esc/Q = quit
Note: the ripples show how strong the motion is, NOT where it is. One board + one router tells you
THAT something moved and how strongly, not WHERE.
"""
import argparse, math, random, re, sys, threading, time
from collections import deque

import numpy as np
import serial
from PyQt5.QtCore import Qt, QTimer, QRectF, QPointF
from PyQt5.QtGui import (QColor, QFont, QPainter, QPen, QBrush, QPainterPath, QRadialGradient)
from PyQt5.QtWidgets import QApplication, QWidget

CAL_SECONDS = 5
DETECT_RATIO = 1.8      # motion > floor * this  -> detected
RELEASE_RATIO = 1.45    # drops below this -> released (hysteresis)
FULL_RATIO = 4.0        # level bar is full at floor * this
HOLD_S = 0.8            # keep "detected" this long after the last peak
NSUB = 64
LINE_RE = re.compile(r'^CSI_DATA,(\d+),([0-9a-fA-F:]+),(-?\d+),.*?,"\[(.*)\]"')


class CsiEngine:
    """Reads serial in a thread, keeps the motion level."""

    def __init__(self, port, baud, record=None):
        self.port, self.baud = port, baud          # baud None = auto-detect
        self.state_mode = False
        self.last_state = 0.0
        self.state_level, self.cal_left, self.pps_val = 0.0, CAL_SECONDS, 0
        self.record = open(record, 'a') if record else None
        self.base = None
        self.motion = 0.0
        self.rssi = 0
        self.pkts = 0
        self.last_pkt = 0.0
        self.times = deque(maxlen=400)
        self.error = None
        self.cal_start = time.time()
        self.cal_sum, self.cal_n = 0.0, 0
        self.calibrating = True
        self.floor = 0.02
        self.detected = False
        self.detect_until = 0.0
        self.history = deque(maxlen=900)          # (time, ratio)
        self._stop = False
        threading.Thread(target=self._run, daemon=True).start()

    def recalibrate(self):
        self.base = None
        self.calibrating = True
        self.cal_start = time.time()
        self.cal_sum, self.cal_n = 0.0, 0
        self.detected = False

    def stop(self):
        self._stop = True

    def calibration_left(self):
        if self.state_mode:
            return self.cal_left
        return max(0, CAL_SECONDS - int(time.time() - self.cal_start))

    def pps(self):
        if self.state_mode:
            return self.pps_val
        now = time.time()
        return sum(1 for t in list(self.times) if now - t <= 1.0)

    def _open(self, baud):
        s = serial.Serial()
        s.port, s.baudrate, s.timeout = self.port, baud, 0.3
        s.dtr = False
        s.rts = False
        s.open()
        return s

    def _run(self):
        bauds = [self.baud] if self.baud else [921600, 115200]
        while not self._stop:
            try:
                s = self._open(bauds[0])
            except Exception as e:
                self.error = f'Cannot open {self.port}: {e}'
                time.sleep(1.0)
                continue
            self.error = None
            try:
                s.write(b'c')                  # make sure the raw CSI stream is off (WiRadar_C6)
            except Exception:
                pass
            self._pump(s, bauds)
            try:
                s.close()
            except Exception:
                pass

    def _pump(self, s, bauds):
        """Read lines. While nothing valid is seen, change baud on the SAME open port (re-opening
        the port would reset the board)."""
        buf, seen, idx = b'', False, 0
        deadline = time.time() + 10.0          # a reset board needs a few seconds to boot and join Wi-Fi
        while not self._stop:
            try:
                buf += s.read(8192)
            except Exception as e:
                self.error = f'Serial error: {e}'
                time.sleep(1.0)
                return
            while b'\n' in buf:
                line, buf = buf.split(b'\n', 1)
                if line.startswith(b'STATE,') or line.startswith(b'CSI_DATA'):
                    seen = True
                    self.error = None
                    self._line(line.decode(errors='ignore'))
            if len(buf) > 20000:
                buf = buf[-2000:]
            if not seen and len(bauds) > 1 and time.time() > deadline:
                idx = (idx + 1) % len(bauds)
                s.baudrate = bauds[idx]
                buf = b''
                deadline = time.time() + 6.0

    def _state(self, line):
        try:
            f = line.strip().split(',')[1:]
            ratio, level, det, cal, cal_left, data, rssi, pps, floor = (
                float(f[0]), float(f[1]), int(f[2]), int(f[3]), int(f[4]), int(f[5]), int(f[6]), int(f[7]), float(f[8]))
        except (ValueError, IndexError):
            return
        now = time.time()
        self.state_mode = True
        self.last_state = now
        self.floor = max(floor, 1e-6)
        self.motion = ratio * self.floor
        self.state_level = level
        self.calibrating = bool(cal)
        self.cal_left = cal_left
        self.detected = bool(det) and not cal and bool(data)
        self.rssi = rssi
        self.pps_val = pps
        if data:
            self.last_pkt = now
            self.pkts += 1
        self.history.append((now, ratio))

    def _line(self, line):
        if line.startswith('STATE,'):
            self._state(line)
            return
        if self.state_mode and time.time() - self.last_state < 1.5:
            return                             # board already sends STATE; ignore raw lines
        self.state_mode = False
        m = LINE_RE.match(line)
        if not m:
            return
        try:
            vals = np.array([int(x) for x in m.group(4).split(',')], dtype=np.float32)
        except ValueError:
            return
        n = min(len(vals) // 2, NSUB)
        if n < 16:
            return
        im, re_ = vals[0:2 * n:2], vals[1:2 * n:2]
        a = np.sqrt(re_ * re_ + im * im)
        mean = a.mean()
        if mean <= 0:
            return
        a = a / mean
        self.rssi = int(m.group(3))
        now = time.time()
        self.times.append(now)
        self.last_pkt = now
        self.pkts += 1
        if self.record:
            self.record.write(line + '\n')
        if self.base is None or len(self.base) != n:
            self.base = a.copy()
            return
        used = (a > 0.05) | (self.base > 0.05)
        if not used.any():
            return
        d = float(np.abs(a - self.base)[used].mean())
        self.base += 0.003 * (a - self.base)
        self.motion = 0.95 * self.motion + 0.05 * d

        if self.calibrating:
            self.cal_sum += self.motion
            self.cal_n += 1
            if now - self.cal_start >= CAL_SECONDS:
                if self.cal_n:
                    self.floor = max(self.cal_sum / self.cal_n, 0.004)
                self.calibrating = False
        else:
            if self.motion > self.floor * DETECT_RATIO:
                self.detected = True
                self.detect_until = now + HOLD_S
            elif self.detected and self.motion < self.floor * RELEASE_RATIO and now > self.detect_until:
                self.detected = False
        self.history.append((now, self.motion / self.floor if not self.calibrating else 1.0))

    @property
    def level(self):
        if self.state_mode:
            return self.state_level
        if self.calibrating:
            return 0.0
        return float(np.clip((self.motion - self.floor) / (self.floor * (FULL_RATIO - 1)), 0, 1))


class Radar(QWidget):
    def __init__(self, eng):
        super().__init__()
        self.eng = eng
        self.sweep = 0.0
        self.ripples = []
        self.last_blip = 0.0
        self.setWindowTitle('Wi-Fi CSI Radar')
        self.resize(1100, 640)
        self.setMinimumSize(760, 460)
        self.setFocusPolicy(Qt.StrongFocus)
        self.t = QTimer(self)
        self.t.timeout.connect(self.tick)
        self.t.start(33)

    def keyPressEvent(self, e):
        k = e.key()
        if k == Qt.Key_R:
            self.eng.recalibrate()
        elif k == Qt.Key_F:
            self.showNormal() if self.isFullScreen() else self.showFullScreen()
        elif k in (Qt.Key_Escape, Qt.Key_Q):
            self.close()

    def closeEvent(self, e):
        self.eng.stop()
        super().closeEvent(e)

    def tick(self):
        self.sweep = (self.sweep + 3.2) % 360
        now = time.time()
        lvl = self.eng.level
        if self.eng.detected and now - self.last_blip > (0.7 - 0.45 * lvl):
            self.last_blip = now
            self.ripples.append(now)
        self.ripples = [t for t in self.ripples if now - t < 1.6]
        self.update()

    # ------------------------------------------------------------------ drawing
    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        w, h = self.width(), self.height()
        p.fillRect(self.rect(), QColor(8, 12, 10))
        side = min(h - 40, int(w * 0.62) - 20)
        cx, cy, R = 20 + side / 2, h / 2, side / 2 - 6
        self.draw_radar(p, cx, cy, R)
        self.draw_panel(p, int(w * 0.62) + 10, 20, w - int(w * 0.62) - 30, h - 40)
        p.end()

    def draw_radar(self, p, cx, cy, R):
        e = self.eng
        det = e.detected and not e.calibrating
        main = QColor(255, 60, 50) if det else QColor(0, 230, 110)
        grid = QColor(main.red(), main.green(), main.blue(), 70)

        g = QRadialGradient(QPointF(cx, cy), R)
        g.setColorAt(0, QColor(0, 40, 20) if not det else QColor(60, 10, 10))
        g.setColorAt(1, QColor(4, 10, 6))
        p.setBrush(QBrush(g))
        p.setPen(QPen(main, 2.5 if det else 1.5))
        p.drawEllipse(QPointF(cx, cy), R, R)
        p.setBrush(Qt.NoBrush)
        p.setPen(QPen(grid, 1))
        for f in (1 / 3, 2 / 3):
            p.drawEllipse(QPointF(cx, cy), R * f, R * f)
        p.drawLine(QPointF(cx - R, cy), QPointF(cx + R, cy))
        p.drawLine(QPointF(cx, cy - R), QPointF(cx, cy + R))
        for a in range(0, 360, 30):
            r1 = R * 0.96
            p.drawLine(QPointF(cx + r1 * math.cos(math.radians(a)), cy + r1 * math.sin(math.radians(a))),
                       QPointF(cx + R * math.cos(math.radians(a)), cy + R * math.sin(math.radians(a))))

        # sweep trail (fading wedges) + beam
        for k in range(40, -1, -1):
            a0 = self.sweep - k * 1.6
            alpha = int(120 * (1 - k / 41) ** 2)
            path = QPainterPath()
            path.moveTo(cx, cy)
            path.arcTo(QRectF(cx - R, cy - R, 2 * R, 2 * R), -a0, 1.7)
            path.closeSubpath()
            p.setPen(Qt.NoPen)
            p.setBrush(QColor(main.red(), main.green(), main.blue(), alpha))
            p.drawPath(path)
        a = math.radians(self.sweep)
        p.setPen(QPen(main, 2.5))
        p.drawLine(QPointF(cx, cy), QPointF(cx + R * math.cos(a), cy + R * math.sin(a)))

        # expanding ripples + pulsing glow in the centre (strength only, no position)
        now = time.time()
        for t0 in self.ripples:
            age = min(max((now - t0) / 1.6, 0.0), 1.0)
            alpha = int(230 * (1 - age) ** 1.5)
            p.setBrush(Qt.NoBrush)
            p.setPen(QPen(QColor(255, 90, 70, alpha), 3.0 * (1 - age) + 1))
            p.drawEllipse(QPointF(cx, cy), R * age, R * age)
        if det:
            lvl = e.level
            gr = R * (0.16 + 0.34 * lvl) * (1 + 0.07 * math.sin(now * 9))
            rg = QRadialGradient(QPointF(cx, cy), gr)
            rg.setColorAt(0, QColor(255, 200, 180, 235))
            rg.setColorAt(0.35, QColor(255, 90, 60, 170))
            rg.setColorAt(1, QColor(255, 40, 20, 0))
            p.setPen(Qt.NoPen)
            p.setBrush(QBrush(rg))
            p.drawEllipse(QPointF(cx, cy), gr, gr)

        if e.calibrating:
            left = e.calibration_left()
            box = QRectF(cx - R * 0.62, cy - R * 0.2, R * 1.24, R * 0.4)
            p.setBrush(QColor(0, 0, 0, 215))
            p.setPen(QPen(QColor(255, 200, 0), 2))
            p.drawRoundedRect(box, 10, 10)
            p.setPen(QColor(255, 200, 0))
            p.setFont(QFont('DejaVu Sans', max(10, int(R * 0.07)), QFont.Bold))
            p.drawText(box.adjusted(0, 0, 0, -box.height() * 0.38), Qt.AlignCenter, 'CALIBRATING...')
            p.setFont(QFont('DejaVu Sans', max(9, int(R * 0.05))))
            p.drawText(box.adjusted(0, box.height() * 0.45, 0, 0), Qt.AlignCenter,
                       f'Keep the area clear  ({left}s)')

    def draw_panel(self, p, x, y, w, h):
        e = self.eng
        now = time.time()
        waiting = (now - e.last_pkt) > 2.0
        det = e.detected and not e.calibrating

        # status
        p.setFont(QFont('DejaVu Sans', 11, QFont.Bold))
        p.setPen(QColor(120, 140, 130))
        p.drawText(QRectF(x, y, w, 24), Qt.AlignLeft | Qt.AlignVCenter, 'STATUS')
        if e.error:
            txt, col = 'SERIAL ERROR', QColor(255, 90, 70)
        elif waiting:
            txt, col = 'NO DATA', QColor(255, 200, 0)
        elif e.calibrating:
            txt, col = 'CALIBRATING', QColor(255, 200, 0)
        elif det:
            txt, col = 'MOTION DETECTED', QColor(255, 70, 55)
        else:
            txt, col = 'AREA CLEAR', QColor(0, 230, 110)
        p.setFont(QFont('DejaVu Sans', 24, QFont.Bold))
        p.setPen(col)
        p.drawText(QRectF(x, y + 26, w, 48), Qt.AlignLeft | Qt.AlignVCenter, txt)
        if e.error or waiting:
            p.setFont(QFont('DejaVu Sans', 10))
            p.setPen(QColor(170, 180, 175))
            msg = e.error or ('No CSI packets yet. Join the board\'s Wi-Fi with a phone, or connect it to your router.\nClose Arduino Serial Monitor.')
            p.drawText(QRectF(x, y + 76, w, 48), Qt.TextWordWrap, msg)

        # level bar
        by = y + 150
        p.setFont(QFont('DejaVu Sans', 11, QFont.Bold))
        p.setPen(QColor(120, 140, 130))
        p.drawText(QRectF(x, by - 26, w, 22), Qt.AlignLeft | Qt.AlignVCenter, 'MOTION LEVEL')
        p.setPen(QPen(QColor(90, 100, 95), 1))
        p.setBrush(Qt.NoBrush)
        p.drawRoundedRect(QRectF(x, by, w, 22), 6, 6)
        lw = (w - 4) * e.level
        if lw > 1:
            p.setPen(Qt.NoPen)
            p.setBrush(QColor(255, 70, 55) if det else QColor(0, 200, 100))
            p.drawRoundedRect(QRectF(x + 2, by + 2, lw, 18), 4, 4)
        thr = (DETECT_RATIO - 1) / (FULL_RATIO - 1)
        p.setPen(QPen(QColor(255, 200, 0), 2))
        p.drawLine(QPointF(x + 2 + (w - 4) * thr, by - 3), QPointF(x + 2 + (w - 4) * thr, by + 25))

        # history graph (motion / floor)
        gy, gh = by + 70, h - (by - y) - 150
        gh = max(gh, 120)
        p.setFont(QFont('DejaVu Sans', 11, QFont.Bold))
        p.setPen(QColor(120, 140, 130))
        p.drawText(QRectF(x, gy - 26, w, 22), Qt.AlignLeft | Qt.AlignVCenter, 'LAST 30 s')
        rect = QRectF(x, gy, w, gh)
        p.setPen(QPen(QColor(40, 55, 48), 1))
        p.setBrush(QColor(12, 18, 15))
        p.drawRoundedRect(rect, 6, 6)
        ymax = max(FULL_RATIO + 0.5, 1.2)
        def Y(r):
            return rect.bottom() - 4 - (rect.height() - 8) * min(max(r, 0), ymax) / ymax
        p.setPen(QPen(QColor(255, 200, 0), 1, Qt.DashLine))
        p.drawLine(QPointF(rect.left(), Y(DETECT_RATIO)), QPointF(rect.right(), Y(DETECT_RATIO)))
        p.setPen(QPen(QColor(70, 90, 80), 1, Qt.DotLine))
        p.drawLine(QPointF(rect.left(), Y(1.0)), QPointF(rect.right(), Y(1.0)))
        pts = [(t, r) for t, r in list(e.history) if now - t <= 30]
        if len(pts) > 2:
            path = QPainterPath()
            for i, (t, r) in enumerate(pts):
                px = rect.right() - (now - t) / 30 * rect.width()
                (path.moveTo if i == 0 else path.lineTo)(px, Y(r))
            p.setBrush(Qt.NoBrush)
            p.setPen(QPen(QColor(255, 90, 70) if det else QColor(0, 220, 120), 2))
            p.drawPath(path)

        # numbers
        ny = rect.bottom() + 16
        p.setFont(QFont('DejaVu Sans', 10))
        p.setPen(QColor(150, 165, 158))
        p.drawText(QRectF(x, ny, w, 22), Qt.AlignLeft,
                   f'{e.pps()} pkt/s   RSSI {e.rssi} dBm   floor {e.floor:.4f}')
        p.drawText(QRectF(x, ny + 22, w, 22), Qt.AlignLeft, 'R = recalibrate    F = fullscreen    Esc = quit')


def main():
    ap = argparse.ArgumentParser(description='Wi-Fi CSI radar viewer')
    ap.add_argument('-p', '--port', default='/dev/ttyACM0')
    ap.add_argument('-b', '--baud', type=int, default=None, help='default: auto-detect (115200 / 921600)')
    ap.add_argument('-r', '--record', help='append raw CSI_DATA lines to this file')
    ap.add_argument('--grab', help='(testing) save a screenshot after 12 s and exit')
    a = ap.parse_args()

    eng = CsiEngine(a.port, a.baud, a.record)
    app = QApplication(sys.argv)
    w = Radar(eng)
    w.show()
    if a.grab:
        def snap():
            w.grab().save(a.grab)
            app.quit()
        QTimer.singleShot(12000, snap)
    app.exec_()
    eng.stop()


if __name__ == '__main__':
    main()
