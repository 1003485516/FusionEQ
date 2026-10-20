# -*- coding: utf-8 -*-
"""旋钮贴图边缘去毛刺：
1) alpha 收边 1px（MinFilter）+ 轻羽化 —— 去掉抠图残留的亮色镶边带；
2) 镶边/透明区 RGB 用不透明区颜色扩散回填 —— 缩放插值不再泛出背景色。
只动 alpha 边缘与透明区，内部像素原样保留。"""
from PIL import Image, ImageFilter
import numpy as np, glob, os, shutil

FILES = ["knob_api215", "knob_api_air", "knob_api_freq", "knob_api_gain",
         "knob_bandwidth", "knob_blue", "knob_blue_glass", "knob_manley", "knob_red"]
RES = os.path.join(os.path.dirname(__file__), "..", "Resources")
BAK = os.path.join(os.path.dirname(__file__), "backup_knobs")
os.makedirs(BAK, exist_ok=True)


def diffuse(rgb, known, iters=80):
    """把 known 区的 RGB 扩散到未知区（简单迭代平均）。"""
    f = rgb.astype(np.float64) * known[:, :, None]
    w = known.astype(np.float64)
    for _ in range(iters):
        fs = np.zeros_like(f); ws = np.zeros_like(w)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                fs += np.roll(np.roll(f, dy, 0), dx, 1)
                ws += np.roll(np.roll(w, dy, 0), dx, 1)
        ok = ws > 0
        ok3 = ok[:, :, None]
        new = np.where(ok3, fs / np.maximum(ws, 1e-9)[:, :, None], f)
        f = np.where(known[:, :, None], f, new)
        w = np.where(known, w, ok.astype(np.float64))   # 已扩散到的像素视为已知
    return f


for name in FILES:
    path = os.path.join(RES, name + ".png")
    bak = os.path.join(BAK, name + ".png")
    if not os.path.exists(bak):
        shutil.copy2(path, bak)

    im = Image.open(path).convert("RGBA")
    arr = np.array(im)
    a = arr[:, :, 3].copy()
    rgb = arr[:, :, :3]

    # 1) alpha 收边 + 羽化
    a_im = Image.fromarray(a, "L").filter(ImageFilter.MinFilter(3)) \
                .filter(ImageFilter.GaussianBlur(0.7))
    a2 = np.array(a_im)
    a2[a2 < 8] = 0

    # 2) 颜色扩散回填（known = 原始不透明区，回填整个非不透明区）
    known = a >= 250
    fill = diffuse(rgb, known)
    out = arr.copy()
    edge_band = ~known                       # 半透明 + 全透明统一回填
    out[:, :, :3] = np.where(edge_band[:, :, None],
                             np.clip(fill, 0, 255).astype(np.uint8), rgb)
    out[:, :, 3] = a2

    Image.fromarray(out, "RGBA").save(path)
    print(name, "cleaned  alpha_px:", int((a2 > 0).sum()))
print("done")
