#!/usr/bin/env python3
"""
WiRadar-C6 enclosure, LANDSCAPE layout, slot-in assembly (no glue, no screws except the 4 outer cover screws).

Looking at the FRONT face (the side with the screen):

    [ display ] | [ DHT11 ] | [ ESP32-C6 board ->USB ]          button = top wall, above the board

  display  : 1.44" module lying on its side, header on the RIGHT. Slides in through a slot in the LEFT wall,
             guided by two rails (top / bottom edge); an end wall stops it. Window in the front wall.
  ESP32-C6 : rests on small ribs behind the front wall (component side to the front), centred by guides,
             clamped by two ribs on the back cover. Both USB-C ports leave through one slot in the RIGHT wall.
  DHT11    : module in a 3-sided pocket, sensor facing vent slits in the front wall, clamped by a cover rib.
  button   : 12x12 tact switch slides into a tunnel under the TOP wall, cap through a hole, clamped by a cover rib.
  cover    : back plate, 4 x M2 x 8 mm self-tapping screws into corner posts.

Edit PARAMS (millimetres), run from this folder:   ../csi-venv/bin/python make_case.py
Output: out/body.stl, out/cover.stl, out/fit_*.stl (small test coupons: print these first!), viewer.html
"""
import json, os
import numpy as np
import trimesh
from shapely.geometry import box as sbox
from trimesh.creation import box as tbox, cylinder as tcyl, extrude_polygon

# ============================ PARAMS (from the product photos; verify with a ruler) ============================
P = dict(
    # 1.44" TFT module PCB, turned 90 degrees: long side horizontal, 8-pin header on the right edge.
    # Scale taken from the 2.54 mm header pitch in the photo: PCB ~29.0 x 39.3 mm.
    DISP_L=39.3, DISP_H=29.0, DISP_T=4.2,     # length (x), height (y), thickness incl. glass
    WIN=27.0, WIN_DX=3.3,                      # window size; its centre sits WIN_DX towards the header (photo)
    # ESP32-C6 board (spec sheet): 46.2 x 28 mm, both USB-C on one short end
    ESP_L=46.2, ESP_W=28.0, ESP_T=1.6,
    ESP_FRONT_Z=9.0,                           # depth of the board's front face behind the case front (room for parts + USB plug)
    USB_SLOT_W=26.0, USB_SLOT_H=7.5,
    # DHT11 module (datasheet drawing): 32 x 14 mm, sensor ~7 mm tall
    DHT_L=32.0, DHT_W=14.0, DHT_T=1.6, DHT_SENSOR_H=7.0, DHT_SENSOR_DY=4.0,
    # 12 x 12 mm tact switch with round cap (photo)
    BTN_BODY=12.0, BTN_H=4.3, BTN_HOLE=7.6, BTN_LEG=3.0, BTN_X_FROM_ESP_LEFT=14.0,
    # case
    WALL=2.4, CLR=0.3, CORNER_R=3.0, COVER_T=2.4, RIB=1.2, DIV=2.0,
    WIRE_ZONE=22.0,                            # depth behind the boards for dupont plugs and wires (soldered wires: ~10)
    SCREW_PILOT=1.8, SCREW_CLEAR=2.4, SCREW_HEAD=4.4,    # M2 self-tapping
)
# ===================================================================================================================

p = P
WALL, CLR, RIB, DIV = p['WALL'], p['CLR'], p['RIB'], p['DIV']

# ---- plan (x centred on 0) ----
Z1W = p['DISP_L'] + 2 * CLR                    # display zone
Z2W = p['DHT_W'] + 2 * CLR + 2 * RIB           # DHT zone
Z3W = p['ESP_L'] + 0.8                         # board zone
IW = Z1W + DIV + Z2W + DIV + Z3W
IH = p['ESP_W'] + 0.8 + (p['BTN_H'] + p['BTN_LEG'] + 1.5)    # board + room above it for the button
OW, OH = IW + 2 * WALL, IH + 2 * WALL
XL = -IW / 2
X1A, X1B = XL, XL + Z1W                        # display zone
X2A = X1B + DIV; X2B = X2A + Z2W               # DHT zone
X3A = X2B + DIV; X3B = X3A + Z3W               # board zone (= XR)
XR = IW / 2
Y0, Y1 = -IH / 2, IH / 2

# ---- depth ----
Z_DISP_F, Z_DISP_B = WALL, WALL + p['DISP_T']
Z_DHT_F = WALL + p['DHT_SENSOR_H'] + 1.0; Z_DHT_B = Z_DHT_F + p['DHT_T']
Z_ESP_F = p['ESP_FRONT_Z'];              Z_ESP_B = Z_ESP_F + p['ESP_T']
Z_END = float(np.ceil(max(Z_ESP_B, Z_DHT_B) + p['WIRE_ZONE']))
D_TOTAL = Z_END + p['COVER_T']
Z_DIV = 14.0                                   # divider walls stop here so wires can cross over them


# ---- helpers (meshes stay watertight; booleans use manifold) ----
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

def cyy(x, z, r, y0, y1, sections=40):          # cylinder along Y
    m = tcyl(radius=r, height=y1 - y0, sections=sections)
    m.apply_transform(trimesh.transformations.rotation_matrix(np.pi / 2, [1, 0, 0]))
    m.apply_translation([x, (y0 + y1) / 2, z])
    return m

def U(*ms): return trimesh.boolean.union(list(ms), engine='manifold')
def D(a, *ms): return trimesh.boolean.difference([a] + list(ms), engine='manifold')
def I(a, b): return trimesh.boolean.intersection([a, b], engine='manifold')


# ================================ BODY ================================
body = D(rbox(OW, OH, Z_END, p['CORNER_R']), rbox(IW, IH, Z_END, max(p['CORNER_R'] - WALL, 0.5), z0=WALL))
add, cut = [], []

# corner screw posts (back end) with pilot holes
for sx in (-1, 1):
    for sy in (-1, 1):
        px, py = sx * (IW / 2 - 1.6), sy * (IH / 2 - 1.6)
        add.append(cz(px, py, 3.4, Z_END - 7.0, Z_END))
        cut.append(cz(px, py, p['SCREW_PILOT'] / 2, Z_END - 6.0, Z_END + 1))

# ---- display: window, slot in the left wall, rails, end wall ----
hy = (p['DISP_H'] + CLR) / 2                    # half height of the channel
zb = Z_DISP_B + 0.4                             # channel back face
pcb_cx = X1A + CLR + p['DISP_L'] / 2
cut.append(bx(pcb_cx + p['WIN_DX'] - p['WIN'] / 2, pcb_cx + p['WIN_DX'] + p['WIN'] / 2, -p['WIN'] / 2, p['WIN'] / 2, -1, WALL + 0.5))
cut.append(bx(-OW / 2 - 1, X1A + 0.01, -hy, hy, WALL, zb))                    # insertion slot, through the left wall
for sgn in (-1, 1):                                                           # top / bottom rail: guide block + retaining lip
    ya, yb = (hy, Y1 + 0.01) if sgn > 0 else (Y0 - 0.01, -hy)
    add.append(bx(X1A, X1B, ya, yb, WALL - 0.01, zb + 1.2))
    la, lb = (hy - 1.4, hy) if sgn > 0 else (-hy, -hy + 1.4)
    add.append(bx(X1A, X1B, la, lb, zb - 0.01, zb + 1.2))
add.append(bx(X1B, X1B + DIV, Y0 - 0.01, Y1 + 0.01, WALL - 0.01, Z_DIV))      # end stop / divider (header side)

# ---- DHT11: 3-sided pocket, vents in the front wall ----
z2c = (X2A + X2B) / 2
L2, W2 = p['DHT_L'], p['DHT_W']
xa, xb_ = X2A + RIB + CLR, X2B - RIB - CLR     # PCB x range
add.append(bx(X2A, X2A + RIB, -L2 / 2 - RIB, L2 / 2 + RIB, WALL - 0.01, Z_DHT_B + 0.2))   # side ribs
add.append(bx(X2B - RIB, X2B, -L2 / 2 - RIB, L2 / 2 + RIB, WALL - 0.01, Z_DHT_B + 0.2))
add.append(bx(X2A, X2B, L2 / 2 + CLR, L2 / 2 + CLR + RIB, WALL - 0.01, Z_DHT_B + 0.2))     # top stop
for xs0, xs1 in ((X2A, X2A + 4.0), (X2B - 4.0, X2B)):                                       # bottom stop in 2 pieces (pins / wires pass)
    add.append(bx(xs0, xs1, -L2 / 2 - CLR - RIB, -L2 / 2 - CLR, WALL - 0.01, Z_DHT_B + 0.2))
for sx in (-1, 1):                                                                          # 4 posts under the PCB corners
    for sy in (-1, 1):
        cx = (xa + 1.0) if sx < 0 else (xb_ - 1.0)
        add.append(bx(cx - 1.0, cx + 1.0, sy * (L2 / 2 - 1.0) - 1.0, sy * (L2 / 2 - 1.0) + 1.0, WALL - 0.01, Z_DHT_F))
for i in range(5):                                                                          # vent slits over the sensor
    yv = p['DHT_SENSOR_DY'] + (i - 2) * 3.0
    cut.append(bx(z2c - 5.5, z2c + 5.5, yv - 0.8, yv + 0.8, -1, WALL + 0.5))

# ---- ESP32-C6: ribs behind the front wall, guides, USB slot in the right wall ----
yb_ = Y0 + 0.4                                  # board bottom edge
yt_ = yb_ + p['ESP_W']
yc_ = (yb_ + yt_) / 2
for xs in (X3A + 9.0, X3B - 13.0):              # rest ribs touching the board's front face along both long edges
    add.append(bx(xs, xs + 6.0, yb_, yb_ + 1.0, WALL - 0.01, Z_ESP_F))
    add.append(bx(xs, xs + 6.0, yt_ - 1.0, yt_, WALL - 0.01, Z_ESP_F))
    add.append(bx(xs, xs + 6.0, yt_ + 0.4, yt_ + 1.6, Z_ESP_F - 1.0, Z_ESP_B + 1.5))       # centring guide above the board
add.append(bx(X3A, X3A + DIV * 0.0 + 0.01, Y0, Y1, WALL, Z_DIV))                            # (left stop is the divider)
add.append(bx(X2B, X3A, Y0 - 0.01, Y1 + 0.01, WALL - 0.01, Z_DIV))                          # divider between DHT and board
usb_zc = Z_ESP_F - 1.6
cut.append(bx(XR - 0.01, OW / 2 + 1, yc_ - p['USB_SLOT_W'] / 2, yc_ + p['USB_SLOT_W'] / 2, usb_zc - p['USB_SLOT_H'] / 2, usb_zc + p['USB_SLOT_H'] / 2))

# ---- button: tunnel under the top wall, hole for the cap ----
BX = X3A + p['BTN_X_FROM_ESP_LEFT']
bz0 = WALL + 0.3; bz1 = bz0 + p['BTN_BODY'] + 0.3
yb_floor = Y1 - p['BTN_H'] - 0.3
for sgn in (-1, 1):
    xi = BX + sgn * (p['BTN_BODY'] / 2 + CLR)
    add.append(bx(min(xi, xi + sgn * RIB), max(xi, xi + sgn * RIB), yb_floor - 0.2, Y1 + 0.01, WALL - 0.01, bz1 + 1.0))   # tunnel walls
    add.append(bx(min(xi, xi - sgn * 0.8), max(xi, xi - sgn * 0.8), yb_floor - 0.8, yb_floor, (bz0 + bz1) / 2 - 1.5, (bz0 + bz1) / 2 + 1.5))  # lips under the body
cut.append(cyy(BX, (bz0 + bz1) / 2, p['BTN_HOLE'] / 2, Y1 - 0.5, OH / 2 + 1))

body = U(body, *add)
body = D(body, *cut)

# ================================ COVER ================================
cover = rbox(OW, OH, p['COVER_T'], p['CORNER_R'], z0=Z_END)
ribs = []
for xs in (X3A + 12.0, X3A + 34.0):                                    # clamp the board (centre band, between the pin rows)
    ribs.append(bx(xs - 1.5, xs + 1.5, yc_ - 4, yc_ + 4, Z_ESP_B, Z_END + 0.01))
ribs.append(bx(z2c - 1.5, z2c + 1.5, -3, 3, Z_DHT_B, Z_END + 0.01))     # clamp the DHT11
ribs.append(bx(BX - 2, BX + 2, Y1 - p['BTN_H'] + 0.3, Y1 - 0.5, bz1, Z_END + 0.01))   # clamp the button
cover = U(cover, *ribs)
holes = []
for sx in (-1, 1):
    for sy in (-1, 1):
        px, py = sx * (IW / 2 - 1.6), sy * (IH / 2 - 1.6)
        holes.append(cz(px, py, p['SCREW_CLEAR'] / 2, Z_END - 1, Z_END + p['COVER_T'] + 1))
        holes.append(cz(px, py, p['SCREW_HEAD'] / 2, Z_END + p['COVER_T'] - 1.0, Z_END + p['COVER_T'] + 1))
cover = D(cover, *holes)

# ================================ ghost components (viewer only) ================================
ghost = {}
ghost['display'] = U(bx(X1A + CLR, X1A + CLR + p['DISP_L'], -p['DISP_H'] / 2, p['DISP_H'] / 2, Z_DISP_F, Z_DISP_B),
                     bx(X1A + CLR + p['DISP_L'] - 4, X1A + CLR + p['DISP_L'] - 1.5, -9, 9, Z_DISP_B, Z_DISP_B + 2.5))      # header
ex0 = X3A + 0.4
ghost['esp32'] = U(bx(ex0, ex0 + p['ESP_L'], yb_, yt_, Z_ESP_F, Z_ESP_B),
                   bx(ex0 + 2, ex0 + 20, yb_ + 3, yt_ - 3, Z_ESP_F - 3.3, Z_ESP_F),                       # module + antenna end
                   bx(ex0 + p['ESP_L'] - 9, ex0 + p['ESP_L'], yc_ - 12.5, yc_ + 12.5, Z_ESP_F - 3.2, Z_ESP_F),   # two USB-C
                   bx(ex0 + 3, ex0 + p['ESP_L'] - 11, yb_ + 0.5, yb_ + 3.0, Z_ESP_B, Z_ESP_B + 8.5),         # pin rows
                   bx(ex0 + 3, ex0 + p['ESP_L'] - 11, yt_ - 3.0, yt_ - 0.5, Z_ESP_B, Z_ESP_B + 8.5))
ghost['dht11'] = U(bx(xa, xb_, -L2 / 2, L2 / 2, Z_DHT_F, Z_DHT_B),
                   bx(z2c - 6, z2c + 6, p['DHT_SENSOR_DY'] - 7.5, p['DHT_SENSOR_DY'] + 7.5, Z_DHT_F - p['DHT_SENSOR_H'], Z_DHT_F))
ghost['button'] = U(bx(BX - 6, BX + 6, yb_floor, Y1, bz0, bz0 + p['BTN_BODY']),
                    cyy(BX, (bz0 + bz1) / 2 - 0.15, 3.4, Y1, Y1 + 0.6 + 2.4))

# ================================ fit-test coupons (print these first) ================================
fit = {}
fit['display'] = I(body, bx(-OW / 2 - 1, X1A + 14, Y0 - 1, Y1 + 1, -1, 12))                   # slot + rails + window edge
fit['dht11'] = I(body, bx(X2A - 3, X2B + 3, Y0 - 1, Y1 + 1, -1, 16))                          # pocket + vents
fit['button'] = I(body, bx(BX - 11, BX + 11, Y1 - 12, Y1 + 3, -1, 20))                         # tunnel + hole

# ---- mirror x: looking at the FRONT face the model's +x points to the viewer's LEFT, so flip it to get
#      display on the left and the USB end on the right as drawn in the docstring ----
def mir(m):
    m = m.copy()
    m.apply_transform(np.diag([-1.0, 1.0, 1.0, 1.0]))
    if m.volume < 0:
        m.invert()
    return m


body, cover = mir(body), mir(cover)
ghost = {k: mir(v) for k, v in ghost.items()}
fit = {k: mir(v) for k, v in fit.items()}

# ================================ export ================================
os.makedirs('out', exist_ok=True)
for name, m in (('body', body), ('cover', cover)):
    assert m.is_watertight, name + ' not watertight'
cover_print = cover.copy()
cover_print.apply_transform(trimesh.transformations.rotation_matrix(np.pi, [1, 0, 0]))
cover_print.apply_translation([0, 0, -cover_print.bounds[0][2]])
body.export('out/body.stl'); cover_print.export('out/cover.stl')
for k, m in fit.items():
    m.export(f'out/fit_{k}.stl')


def pack(m, nd=2):
    return dict(v=np.round(m.vertices, nd).flatten().tolist(), f=m.faces.flatten().tolist())


json.dump(dict(body=pack(body), cover=pack(cover), **{k: pack(v) for k, v in ghost.items()},
               dims=dict(OW=OW, OH=OH, D=D_TOTAL, Z_END=Z_END)), open('out/preview_data.json', 'w'))

print(f"outer size  : {OW:.1f} x {OH:.1f} x {D_TOTAL:.1f} mm (W x H x D), landscape")
print(f"inner       : {IW:.1f} x {IH:.1f} mm; wire zone {p['WIRE_ZONE']:.0f} mm behind the boards")
print(f"body  {np.round(body.bounds[1] - body.bounds[0], 1)}  vol {body.volume / 1000:.1f} cm3")
print(f"cover {np.round(cover_print.bounds[1] - cover_print.bounds[0], 1)}  vol {cover.volume / 1000:.1f} cm3")
for k, m in fit.items():
    print(f"fit_{k}: {np.round(m.bounds[1] - m.bounds[0], 1)} mm")

# 3D viewer: one self-contained html (geometry + three.js from lib/)
tpl = open('viewer_template.html').read()
tpl = tpl.replace('/*THREE*/', open('lib/three.min.js').read()).replace('/*ORBIT*/', open('lib/OrbitControls.js').read())
open('viewer.html', 'w').write(tpl.replace('/*DATA*/', open('out/preview_data.json').read()))
print('viewer.html written')
