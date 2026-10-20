# -*- coding: utf-8 -*-
"""硬件按钮去歪斜：顶边直线拟合实测倾角 → 整图绕中心转正。
对每对 off/on 用同一角度（取 off 实测），保证两态不相对旋转。
处理 Resources/btn_{hp,low,mid,high,air,lf,lmf,hmf,hf}_{off,on}.png"""
import os
import math
import numpy as np
from PIL import Image

RES = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Resources")

def top_edge_angle(im):
    """body(alpha>100) 每列顶边的线性拟合斜率 → 倾角（度，顺时针为正）"""
    a = np.asarray(im)[:, :, 3]
    h, w = a.shape
    xs, ys = [], []
    for x in range(w):
        col = np.nonzero(a[:, x] > 100)[0]
        if len(col):
            xs.append(x); ys.append(col[0])
    xs = np.asarray(xs, float); ys = np.asarray(ys, float)
    # 只用中央 70% 列（避开左右圆角）
    lo, hi = np.percentile(xs, 15), np.percentile(xs, 85)
    k = (xs >= lo) & (xs <= hi)
    m, b = np.polyfit(xs[k], ys[k], 1)
    return math.degrees(math.atan(m)), m

names = ["hp", "low", "mid", "high", "air", "lf", "lmf", "hmf", "hf"]
for n in names:
    off_p = os.path.join(RES, f"btn_{n}_off.png")
    on_p  = os.path.join(RES, f"btn_{n}_on.png")
    if not os.path.exists(off_p):
        continue
    off = Image.open(off_p).convert("RGBA")
    ang, m = top_edge_angle(off)
    print(f"{n}: 顶边斜率 {m:+.4f} → 倾角 {ang:+.2f}°，旋转 {-ang:+.2f}°")
    for p in (off_p, on_p):
        if not os.path.exists(p):
            continue
        im = Image.open(p).convert("RGBA")
        # 绕中心转正（画布不变；440/480 画布留有边距，小角度无裁切风险）
        im = im.rotate(-ang, resample=Image.BICUBIC, center=(im.width / 2, im.height / 2))
        im.save(p)

# 预览
tiles = [Image.open(os.path.join(RES, f"btn_{n}_off.png")) for n in names
         if os.path.exists(os.path.join(RES, f"btn_{n}_off.png"))]
w, h = tiles[0].size
pv = Image.new("RGBA", ((w + 8) * len(tiles) + 8, h + 16), (60, 70, 77, 255))
for i, t in enumerate(tiles):
    pv.paste(t, (8 + i * (w + 8), 8), t)
pv.save(os.path.join(os.path.dirname(os.path.abspath(__file__)), "preview_btn_deskew.png"))
print("done")
