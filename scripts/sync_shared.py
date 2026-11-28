#!/usr/bin/env python3
"""Copies the shared firmware headers from firmware/WiRadar_C6/ into firmware/WiRadar_C6_Lite/.

The Arduino build only sees files inside the sketch folder, so the Lite sketch needs its own copy.
Edit the headers in firmware/WiRadar_C6/ only, then run:

    python3 scripts/sync_shared.py            # copy
    python3 scripts/sync_shared.py --check    # exit 1 if the copies are out of date (used by CI)
"""
import os, shutil, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
SRC = os.path.join(ROOT, 'firmware', 'WiRadar_C6')
DST = os.path.join(ROOT, 'firmware', 'WiRadar_C6_Lite')
SHARED = ['config.h', 'csi_core.h', 'csi_math.h', 'wifi_mode.h', 'tft_ui.h', 'climate.h']


def main():
    check = '--check' in sys.argv
    stale = []
    for name in SHARED:
        a, b = os.path.join(SRC, name), os.path.join(DST, name)
        if not os.path.exists(a):
            continue
        same = os.path.exists(b) and open(a, 'rb').read() == open(b, 'rb').read()
        if same:
            continue
        stale.append(name)
        if not check:
            shutil.copyfile(a, b)
    if check:
        if stale:
            print('out of date (run python3 scripts/sync_shared.py):', ', '.join(stale))
            return 1
        print('shared headers are in sync')
        return 0
    print('copied:', ', '.join(stale) if stale else 'nothing to do')
    return 0


if __name__ == '__main__':
    sys.exit(main())
