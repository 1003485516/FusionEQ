# -*- coding: utf-8 -*-
"""LOW/MID/HIGH 硬件开关素材批处理：
棋盘格底 JPG → 边缘洪泛去底（含投影）→ 两态统一画布裁剪 → Resources/btn_{low,mid,high}_{off,on}.png
"""
from PIL import Image
from collections import deque
import os

BASE = r"D:\微信\xwechat_files\lingchen1003_bce1\temp\RWTemp\2026-10\9e20f478899dc29eb19741386f9343c8"
PAIRS = [
    ("low",  "0ab898ebe8b10799db78172669f660a0.jpg", "539ab03699066156884899c90b1ffc3a.jpg"),
    ("mid",  "fc5464b54e1c898a2c43afaaeacf7a20.jpg", "ee745451f5ab8231b37f3e4c4d0097a2.jpg"),
    ("high", "135a12b1d93f3a280deb564a0e11b8e4.jpg", "d385388a714f828b2deb006b2e0a24b5.jpg"),
]
OUT_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Resources")

def dark_bbox(im):
    """按钮主体 = 深色边框（低亮度像素）的包围盒；排除浅灰投影残留"""
    w, h = im.size
    px = im.load()
    l, t, r, b = w, h, 0, 0
    for y in range(h):
        for x in range(w):
            rr, gg, bb, aa = px[x, y]
            if aa > 40 and (rr + gg + bb) / 3.0 < 155:
                if x < l: l = x
                if y < t: t = y
                if x > r: r = x
                if y > b: b = y
    return (l, t, r, b)

def kill_bg(path):
    im = Image.open(path).convert("RGBA")
    w, h = im.size
    px = im.load()

    def is_bg(x, y):
        r, g, b, a = px[x, y]
        mx, mn = max(r, g, b), min(r, g, b)
        return mn >= 175 and (mx - mn) <= 22   # 棋盘格白/灰 + 浅灰投影

    seen = bytearray(w * h)
    q = deque()
    for x in range(w):
        q.append((x, 0)); q.append((x, h - 1))
    for y in range(h):
        q.append((0, y)); q.append((w - 1, y))

    while q:
        x, y = q.popleft()
        if x < 0 or y < 0 or x >= w or y >= h or seen[y * w + x]:
            continue
        seen[y * w + x] = 1
        if not is_bg(x, y):
            continue
        px[x, y] = (0, 0, 0, 0)
        q.extend(((x+1, y), (x-1, y), (x, y+1), (x, y-1)))

    return im

tiles = []
for name, off_p, on_p in PAIRS:
    off, on = kill_bg(os.path.join(BASE, off_p)), kill_bg(os.path.join(BASE, on_p))
    bo, bn = dark_bbox(off), dark_bbox(on)
    l = min(bo[0], bn[0]); t = min(bo[1], bn[1])
    r = max(bo[2], bn[2]); b = max(bo[3], bn[3])
    l = max(0, l - 3); t = max(0, t - 3)
    r = min(off.width, r + 3); b = min(off.height, b + 3)
    for im in (off, on):
        pxx = im.load()
        for y in range(im.height):
            for x in range(im.width):
                if x < l or y < t or x >= r or y >= b:
                    pxx[x, y] = (0, 0, 0, 0)
    off, on = off.crop((l, t, r, b)), on.crop((l, t, r, b))

    tw = 480
    th = round(off.height * tw / off.width)
    off = off.resize((tw, th), Image.LANCZOS)
    on  = on.resize((tw, th), Image.LANCZOS)
    off.save(os.path.join(OUT_DIR, f"btn_{name}_off.png"))
    on.save(os.path.join(OUT_DIR, f"btn_{name}_on.png"))
    print(name, "size:", off.size, " aspect:", round(tw / th, 3))
    tiles.append((name, off, on))

# 预览拼图
pw = 480 * 3 + 40
ph = max(t[1].height for t in tiles) + 20
pv = Image.new("RGBA", (pw, ph * 2 + 10), (245, 246, 248, 255))
for i, (name, off, on) in enumerate(tiles):
    pv.paste(off, (10 + i * (480 + 10), 10), off)
    pv.paste(on,  (10 + i * (480 + 10), ph), on)
pv.save(os.path.join(os.path.dirname(os.path.abspath(__file__)), "preview_btn_lmh.png"))
print("done")
