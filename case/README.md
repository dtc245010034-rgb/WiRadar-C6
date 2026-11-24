# Case for ESP32-C6 radar display

1. Measure your parts with a ruler/caliper and edit the numbers at the top of `make_case.py` (PARAMS).
2. Regenerate:   `../csi-venv/bin/python make_case.py`
3. Open `viewer.html` to check, print `out/body.stl` and `out/cover.stl`.

Assembly: glue/tape display into the front pocket, fix the button with its nut,
put DHT11 in the side pocket, tape ESP32 (components side toward the cover) onto the cover pads,
connect wires, close cover with 4x M2 x 8 mm self-tapping screws.

Print: PLA/PETG, 0.2 mm layers, 3 walls, no supports. Body: front face down. Cover: as exported.
