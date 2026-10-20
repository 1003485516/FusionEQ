# -*- coding: utf-8 -*-
"""API 215L 橙芯银裙旋钮：棋盘格底 JPG → 白/浅灰洪泛去底 → 外圆拟合（上/左右，
   排除底部舌针）→ 舌针角度按残余半径突起实测 → 512² 居中 + 羽化"""
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

SRC = r'C:/Users/Administrator/.workbuddy/clipboard-images/clipboard-2026-10-19T16-56-41-320Z-c28cd73d.jpg'
OUT = 'FusionEQ/Resources/knob_api215.png'
CANVAS = 512

a = np.asarray(Image.open(SRC).convert('RGB')).astype(np.float64)
H, W = a.shape[:2]
lum = a.mean(axis=2)

# ── 1. 背景：白/浅灰棋盘格，从边框洪泛 ──────────────────────────────────
bg_seed = np.zeros((H, W), bool)
bg_seed[0, :] = bg_seed[-1, :] = bg_seed[:, 0] = bg_seed[:, -1] = True
lbl, _ = ndimage.label(bg_seed | (lum > 228))
border = np.unique(np.r_[lbl[0, :], lbl[-1, :], lbl[:, 0], lbl[:, -1]])
border = border[border != 0]
bg = np.isin(lbl, border)
bg |= ndimage.binary_dilation(bg, iterations=3) & (lum > 222)

body = ndimage.binary_fill_holes(~bg)
lbl2, n2 = ndimage.label(body)
szs = ndimage.sum(body, lbl2, range(1, n2 + 1))
body = lbl2 == (np.argmax(szs) + 1)
ys, xs = np.nonzero(body)
print(f'body px {len(xs)}')

# ── 2. 外圆：顶部/左右边缘定圆（底部舌针自然排除）────────────────────────
x0, x1, y0 = xs.min(), xs.max(), ys.min()
cx0 = (x0 + x1) / 2.0
r0 = (x1 - x0) / 2.0
cy0 = y0 + r0

angs = np.degrees(np.arctan2(xs - cx0, -(ys - cy0))) % 360
up = (angs < 120) | (angs > 240)           # 上半圈 + 左右各 60°
px, py = xs[up].astype(float), ys[up].astype(float)
rr = np.sqrt((px - cx0) ** 2 + (py - cy0) ** 2)
edge = rr > np.percentile(rr, 99.2)
ex, ey = px[edge], py[edge]

def fit_circle(x, y):
    A = np.c_[2 * x, 2 * y, np.ones(len(x))]
    b = x * x + y * y
    s, *_ = np.linalg.lstsq(A, b, rcond=None)
    return s[0], s[1], np.sqrt(s[2] + s[0] ** 2 + s[1] ** 2)

cx, cy, R = fit_circle(ex, ey)
for _ in range(3):
    d = np.abs(np.sqrt((ex - cx) ** 2 + (ey - cy) ** 2) - R)
    k = d < max(4.0, np.percentile(d, 75))
    cx, cy, R = fit_circle(ex[k], ey[k])
print(f'circle centre ({cx:.1f},{cy:.1f}) R {R:.1f}')

# ── 3. 舌针角度：下半圈残余半径突起的方位（角度约定 0°=12 点顺时针）───────
dxb, dyb = xs - cx, ys - cy
rb = np.sqrt(dxb ** 2 + dyb ** 2)
va = np.degrees(np.arctan2(dxb, -dyb)) % 360
lower = (va > 60) & (va < 300)             # 下半圈
res = rb - R
tab_bins = {}
for ang, dv in zip(va[lower], res[lower]):
    tab_bins.setdefault(int(ang // 10) * 10, []).append(dv)
best = max(tab_bins.items(), key=lambda kv: np.percentile(kv[1], 90))
tab = float(best[0]) + 5.0
print(f'舌针方位 ~{tab:.0f}°（bin {best[0]}，p90 残差 {np.percentile(best[1], 90):.1f}px）')

# ── 4. 合成：body 掩膜做 alpha（保舌针、无背景光晕），盘占图 0.76 ─────────
mask = Image.fromarray((body * 255).astype(np.uint8), 'L')
mask = mask.filter(__import__('PIL.ImageFilter', fromlist=['ImageFilter']).GaussianBlur(1.6))

HALF = R / 0.76 * 1.005                    # 画布半边 = 盘占图 0.76（舌针 1.24R 仍留 6% 余量）
box = (int(round(cx - HALF)), int(round(cy - HALF)),
       int(round(cx + HALF)), int(round(cy + HALF)))
src_rgba = Image.fromarray(a.astype(np.uint8), 'RGB').convert('RGBA')
src_rgba.putalpha(mask)
l, t, r, b = box
pad_l, pad_t = max(0, -l), max(0, -t)
crop = src_rgba.crop((max(0, l), max(0, t), min(W, r), min(H, b)))
canvas_img = Image.new('RGBA', (int(2 * HALF), int(2 * HALF)), (0, 0, 0, 0))
canvas_img.paste(crop, (pad_l, pad_t))
canvas_img = canvas_img.resize((CANVAS, CANVAS), Image.LANCZOS)
canvas_img.save(OUT)
print(f'盘占图 0.76 → side 系数 = {2.04 / 0.76:.3f}，texOffset = {(360 - tab) % 360:.1f}° '
      f'= {np.radians((360 - tab) % 360):.4f} rad')

dbg = Image.new('RGB', (CANVAS, CANVAS), (34, 38, 44))
fin = Image.open(OUT)
dbg.paste(fin, (0, 0), fin)
dr = ImageDraw.Draw(dbg)
dr.line([(CANVAS / 2, 0), (CANVAS / 2, CANVAS)], fill=(255, 80, 80), width=1)
dr.line([(0, CANVAS / 2), (CANVAS, CANVAS / 2)], fill=(255, 80, 80), width=1)
dbg.resize((300, 300), Image.NEAREST).save('FusionEQ/tools/prev_knob_api215.png')
print('done')
