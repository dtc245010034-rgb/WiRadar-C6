#!/usr/bin/env python3
"""
Parametric enclosure: ESP32-C6 dev board + 1.44" ST7735 TFT + DHT11 + push button.
USB powered only (no battery). Two printed parts + 4x M2 screws:

  body.stl   front plate (display window, button hole), walls, USB slot, vent slots, screw posts
  cover.stl  back plate with ESP32 board pads/guides and 4 screw holes

Edit the numbers in PARAMS (millimetres), run:   ../csi-venv/bin/python make_case.py
Output: out/body.stl, out/cover.stl, out/preview_data.json (used by viewer.html)
"""
import json, os
import numpy as np
import trimesh
from shapely.geometry import box as sbox
from trimesh.creation import box as tbox, cylinder as tcyl, extrude_polygon

# ============================ PARAMS (measure your parts!) ============================
P = dict(
    # --- 1.44" TFT module (PCB) ---
    DW=33.0, DH=37.0, DISP_T=4.0,        # PCB width, height (incl. pin header edge), thickness with glass
    WIN_W=26.5, WIN_H=26.5, WIN_DY=-1.0, # visible window size, offset from PCB centre (+ = up)
    HDR_W=24.0,                          # opening at top of display pocket for the 8-pin header + wires
    # --- ESP32-C6 dev board ---
    ESP_W=25.6, ESP_L=52.0, ESP_T=1.6,   # PCB width, length, thickness
    ESP_COMP_H=4.0,                      # tallest part on the component side (USB connector ~3.3)
    USB_SLOT_W=24.0, USB_SLOT_H=7.5,     # one wide slot for both USB-C ports (plug overmold ~12x6.5)
    USB_X=0.0,                           # slot centre offset from case centre
    # --- push button (panel mount hole) ---
    BTN_D=7.2,                           # hole diameter
    # --- DHT11 module ---
    DHT_A=12.5, DHT_B=21.5, DHT_T=8.0,   # module size along Z, along Y, thickness
    # --- case ---
    WALL=2.4, CLR=0.4, CORNER_R=4.4,
    GAP=24.0,                            # free depth between display back and ESP board (dupont plugs + wires)
    COVER_T=2.4,
    SCREW_PILOT=1.8, SCREW_CLEAR=2.4, SCREW_HEAD=4.4,   # M2 self-tapping
    BTN_FROM_BOTTOM=11.0,
)
# ======================================================================================

p = P
WALL, CLR = p['WALL'], p['CLR']
IW = p['DW'] + 4.0                                 # inner width
IH = p['DH'] + 1.0 + 22.0                          # inner height (display on top, button area below)
OW, OH = IW + 2 * WALL, IH + 2 * WALL
FRONT_T = WALL
POCKET_D = p['DISP_T'] + 0.3
STAND = p['ESP_COMP_H'] + 2.4                      # ESP pad height (>= parts + room for USB slot)
Z_DISP_BACK = FRONT_T + POCKET_D
Z_END = Z_DISP_BACK + p['GAP'] + p['ESP_T'] + STAND   # body open edge / cover inner face
D_TOTAL = Z_END + p['COVER_T']
Y0, Y1 = -IH / 2, IH / 2                           # cavity bottom / top

# ---- helpers: all meshes are watertight; booleans use manifold ----
def rbox(w, h, d, r, z0=0.0, cx=0.0, cy=0.0):
    poly = sbox(-w / 2 + r, -h / 2 + r, w / 2 - r, h / 2 - r).buffer(r, resolution=24)
    m = extrude_polygon(poly, d)
    m.apply_translation([cx, cy, z0])
    return m

def bx(x0, x1, y0, y1, z0, z1):
    m = tbox(extents=[x1 - x0, y1 - y0, z1 - z0])
    m.apply_translation([(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2])
    return m

def cz(x, y, r, z0, z1, sections=40):
    m = tcyl(radius=r, height=z1 - z0, sections=sections)
    m.apply_translation([x, y, (z0 + z1) / 2])
    return m

def U(*ms):  return trimesh.boolean.union(list(ms), engine='manifold')
def D(a, *ms): return trimesh.boolean.difference([a] + list(ms), engine='manifold')

# =========================== BODY ===========================
body = rbox(OW, OH, Z_END, p['CORNER_R'])
cavity = rbox(IW, IH, Z_END, p['CORNER_R'] - WALL, z0=FRONT_T)
body = D(body, cavity)

# corner screw posts (open end) with pilot holes
posts, pilots = [], []
for sx in (-1, 1):
    for sy in (-1, 1):
        px, py = sx * (IW / 2 - 1.6), sy * (IH / 2 - 1.6)
        posts.append(cz(px, py, 3.4, Z_END - 9.0, Z_END))
        pilots.append(cz(px, py, p['SCREW_PILOT'] / 2, Z_END - 8.0, Z_END + 1))
body = U(body, *posts)

# display pocket (ring) with header opening at the top and 4 corner lips
dcy = Y1 - 1.0 - p['DH'] / 2
pw, ph = p['DW'] + CLR, p['DH'] + CLR
ring = D(bx(-pw / 2 - 1.2, pw / 2 + 1.2, dcy - ph / 2 - 1.2, dcy + ph / 2 + 1.2, FRONT_T - 0.01, FRONT_T + POCKET_D),
         bx(-pw / 2, pw / 2, dcy - ph / 2, dcy + ph / 2, FRONT_T - 1, FRONT_T + POCKET_D + 1),
         bx(-p['HDR_W'] / 2, p['HDR_W'] / 2, dcy + ph / 2 - 1, dcy + ph / 2 + 2, FRONT_T - 1, FRONT_T + POCKET_D + 1))
lips = []
for sx in (-1, 1):
    for sy in (-1, 1):
        xo = sx * (pw / 2)
        yo = dcy + sy * (ph / 2)
        lips.append(bx(min(xo, xo - sx * 1.0), max(xo, xo - sx * 1.0),
                       min(yo, yo - sy * 5.0), max(yo, yo - sy * 5.0),
                       FRONT_T + POCKET_D, FRONT_T + POCKET_D + 0.8))
body = U(body, ring, *lips)

# DHT11 pocket on the right wall + vent slots
dht_y = Y1 - 14.0
dht_z = (Z_DISP_BACK + 3.0) + p['DHT_A'] / 2 + 4.0
dfw = 1.0
dht_frame = D(bx(IW / 2 - p['DHT_T'] - 0.4, IW / 2 + 0.01,
                 dht_y - p['DHT_B'] / 2 - dfw, dht_y + p['DHT_B'] / 2 + dfw,
                 dht_z - p['DHT_A'] / 2 - dfw, dht_z + p['DHT_A'] / 2 + dfw),
              bx(IW / 2 - p['DHT_T'] - 1, IW / 2 + 1,
                 dht_y - p['DHT_B'] / 2, dht_y + p['DHT_B'] / 2,
                 dht_z - p['DHT_A'] / 2, dht_z + p['DHT_A'] / 2),
              bx(IW / 2 - p['DHT_T'] - 1, IW / 2 + 1,
                 dht_y - p['DHT_B'] / 2 - dfw - 1, dht_y - p['DHT_B'] / 2 + 3,       # open at the bottom for pins/wires
                 dht_z - p['DHT_A'] / 2 + 1, dht_z + p['DHT_A'] / 2 - 1))
body = U(body, dht_frame)
vents = [bx(IW / 2 - 1.0, OW / 2 + 1, dht_y + p['DHT_B'] / 2 - 4 - i * 3.0 - 0.8, dht_y + p['DHT_B'] / 2 - 4 - i * 3.0 + 0.8,
            dht_z - 5.5, dht_z + 5.5) for i in range(5)]

# cut-outs
win = bx(-p['WIN_W'] / 2, p['WIN_W'] / 2, dcy + p['WIN_DY'] - p['WIN_H'] / 2, dcy + p['WIN_DY'] + p['WIN_H'] / 2, -1, FRONT_T + 0.5)
btn = cz(0, Y0 + p['BTN_FROM_BOTTOM'], p['BTN_D'] / 2, -1, FRONT_T + 1)
usb_zc = Z_END - STAND + 1.6                         # connector centre height above PCB component face
usb = bx(p['USB_X'] - p['USB_SLOT_W'] / 2, p['USB_X'] + p['USB_SLOT_W'] / 2,
         -OH / 2 - 1, Y0 + 0.5, usb_zc - p['USB_SLOT_H'] / 2, usb_zc + p['USB_SLOT_H'] / 2)
body = D(body, win, btn, usb, *vents, *pilots)

# =========================== COVER ===========================
cover = rbox(OW, OH, p['COVER_T'], p['CORNER_R'], z0=Z_END)
ex0, ex1 = -p['ESP_W'] / 2, p['ESP_W'] / 2
ey0 = Y0 + 0.8
ey1 = ey0 + p['ESP_L']
z_pcb_back = Z_END - STAND
z_pcb_front = z_pcb_back - p['ESP_T']
pads, guides = [], []
for sx in (-1, 1):
    for sy in (0, 1):
        xo = sx * (p['ESP_W'] / 2 - 1.5)
        yo = ey0 + 1.5 if sy == 0 else ey1 - 1.5
        pads.append(bx(xo - 1.5, xo + 1.5, yo - 1.5, yo + 1.5, z_pcb_back, Z_END + 0.01))
for sx in (-1, 1):
    for fy in (0.28, 0.72):                          # side guides keep the board centred
        yc = ey0 + p['ESP_L'] * fy
        xi = sx * (p['ESP_W'] / 2 + 0.25)
        xo = sx * (p['ESP_W'] / 2 + 0.25 + 1.2)
        guides.append(bx(min(xi, xo), max(xi, xo), yc - 3, yc + 3, z_pcb_front, Z_END + 0.01))
cover = U(cover, *pads, *guides)
holes = []
for sx in (-1, 1):
    for sy in (-1, 1):
        px, py = sx * (IW / 2 - 1.6), sy * (IH / 2 - 1.6)
        holes.append(cz(px, py, p['SCREW_CLEAR'] / 2, Z_END - 1, Z_END + p['COVER_T'] + 1))
        holes.append(cz(px, py, p['SCREW_HEAD'] / 2, Z_END + p['COVER_T'] - 1.0, Z_END + p['COVER_T'] + 1))
cover = D(cover, *holes)

# =========================== ghost components (preview only) ===========================
ghost = {}
ghost['display'] = bx(-p['DW'] / 2, p['DW'] / 2, dcy - p['DH'] / 2, dcy + p['DH'] / 2, FRONT_T, FRONT_T + p['DISP_T'])
ghost['esp32'] = U(bx(ex0, ex1, ey0, ey1, z_pcb_front, z_pcb_back),
                   bx(-5, 5, ey1 - 12, ey1 - 1, z_pcb_back, z_pcb_back + 3.0),
                   bx(-p['ESP_W'] / 2 + 1, -p['ESP_W'] / 2 + 3.6, ey0 + 2, ey1 - 14, z_pcb_front - 2.5, z_pcb_front),
                   bx(p['ESP_W'] / 2 - 3.6, p['ESP_W'] / 2 - 1, ey0 + 2, ey1 - 14, z_pcb_front - 2.5, z_pcb_front),
                   bx(-6 + p['USB_X'], 6 + p['USB_X'], ey0 - 0.5, ey0 + 8, z_pcb_back, z_pcb_back + 3.2))
ghost['dht11'] = bx(IW / 2 - p['DHT_T'], IW / 2, dht_y - p['DHT_B'] / 2, dht_y + p['DHT_B'] / 2, dht_z - p['DHT_A'] / 2, dht_z + p['DHT_A'] / 2)
ghost['button'] = cz(0, Y0 + p['BTN_FROM_BOTTOM'], p['BTN_D'] / 2 - 0.3, -1.5, FRONT_T + 9)

# =========================== export ===========================
os.makedirs('out', exist_ok=True)
for name, m in (('body', body), ('cover', cover)):
    assert m.is_watertight, name + ' not watertight'
# print orientations: body front-face down; cover flipped so its flat face is on the bed
cover_print = cover.copy()
cover_print.apply_transform(trimesh.transformations.rotation_matrix(np.pi, [1, 0, 0]))
cover_print.apply_translation([0, 0, -cover_print.bounds[0][2]])
body.export('out/body.stl'); cover_print.export('out/cover.stl')

def pack(m, nd=2):
    return dict(v=np.round(m.vertices, nd).flatten().tolist(), f=m.faces.flatten().tolist())
json.dump(dict(body=pack(body), cover=pack(cover), **{k: pack(v) for k, v in ghost.items()},
               dims=dict(OW=OW, OH=OH, D=D_TOTAL, Z_END=Z_END)),
          open('out/preview_data.json', 'w'))

print(f"outer size  : {OW:.1f} x {OH:.1f} x {D_TOTAL:.1f} mm (W x H x D)")
print(f"inner cavity: {IW:.1f} x {IH:.1f} mm, free gap behind display {p['GAP']:.1f} mm")
print(f"body  {body.bounds[1]-body.bounds[0]}  vol {body.volume/1000:.1f} cm3")
print(f"cover {cover_print.bounds[1]-cover_print.bounds[0]}  vol {cover.volume/1000:.1f} cm3")

# 3D viewer (self-contained html with the geometry embedded)
tpl = open('viewer_template.html').read()
open('viewer.html', 'w').write(tpl.replace('/*DATA*/', open('out/preview_data.json').read()))
print('viewer.html written')
