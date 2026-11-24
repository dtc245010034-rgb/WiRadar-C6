#!/usr/bin/env python3
"""Runs Espressif's csi_data_read_parse.py against the WiRadar_C6 firmware.

The tool opens the port itself, which would pulse DTR/RTS and reset the board. This launcher patches
pyserial so the port is opened quietly (DTR/RTS low) and sends 'C' every second to keep the raw CSI stream on
(and 'c' when it exits). Needs the WiRadar_C6 firmware (it only streams raw CSI while 'C' keep-alives arrive).
"""
import atexit, os, runpy, sys, threading, time
import serial

_opened = []

class QuietSerial(serial.Serial):
    def __init__(self, port=None, baudrate=9600, **kw):
        super().__init__(port=None, baudrate=baudrate, **kw)
        self.port = port
        self.dtr = False
        self.rts = False
        self.open()
        self.write(b'C')
        self.flush()
        _opened.append(self)
        threading.Thread(target=self._keepalive, daemon=True).start()

    def _keepalive(self):
        # The board may reset when the port is opened; keep asking for the stream every second.
        while self.is_open:
            time.sleep(1.0)
            try:
                self.write(b'C')
            except Exception:
                return

def _stop_stream():
    for s in _opened:
        try:
            if s.is_open:
                s.write(b'c'); s.flush()
        except Exception:
            pass

serial.Serial = QuietSerial
atexit.register(_stop_stream)
tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'csi_data_read_parse.py')
sys.argv[0] = tool
runpy.run_path(tool, run_name='__main__')
