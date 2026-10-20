# -*- coding: utf-8 -*-
"""HP/LOW/MID/HIGH 硬件开关素材统一处理 v2：
棋盘格底 JPG → 洪泛去底 → 以深色边框主体为基准两态互相对齐缩放 →
四对统一画布（主体同宽、同比例）→ Resources/btn_*_off|on.png
保证：① 同一颗钮 on/off 盘面同大 ② 四颗钮显示大小一致
"""
from PIL import Image
from collections import deque
import os

BASE = r"D:\微信\xwechat_files\lingchen1003_bce1\temp\RWTemp\2026-10\9e20f478899dc29eb19741386f9343c8"
PAIRS = [
    ("hp",   "e2374d4458ad3b564b1eea34ae84e721.jpg", "5fbc750bd9cba208747cdc3279e333a7.jpg"),
    ("low",  "0ab898ebe8b10799db78172669f660a0.jpg", "539ab03699066156884899c90b1ffc3a.jpg"),
    ("mid",  "fc5464b54e1c898a2c43afaaeacf7a20.jpg", "ee745451f5ab8231b37f3e4c4d0097a2.jpg"),
    ("high", "135a12b1d93f3a280deb564a0e11b8e4.jpg", "d385388a714f828b2deb006b2e0a24b5.jpg"),
]
OUT_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Resources")

def kill_bg(path):
    im = Image.open(path).convert("RGBA")
    w, h = im.size
    px = im.load()

    def is_bg(x, y):
        r, g, b, a = px[x, y]
        mx, mn = max(r, g, b), min(r, g, b)
        return mn >= 175 and (mx - mn) <= 22

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

def dark_bbox(im):
    """深色边框主体的包围盒（<155 亮度），不受辉光/投影影响"""
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

def clear_outside(im, box, pad=6):
    l, t, r, b = box
    l = max(0, l - pad); t = max(0, t - pad)
    r = min(im.width, r + pad); b = min(im.height, b + pad)
    pxx = im.load()
    for y in range(im.height):
        for x in range(im.width):
            if x < l or y < t or x >= r or y >= b:
                pxx[x, y] = (0, 0, 0, 0)
    return (l, t, r, b)

BODY_W = 440                      # 主体（边框）统一宽度
CANVAS = (480, 208)               # 统一画布（aspect 2.31）

tiles = []
for name, off_p, on_p in PAIRS:
    off, on = kill_bg(os.path.join(BASE, off_p)), kill_bg(os.path.join(BASE, on_p))
    bo, bn = dark_bbox(off), dark_bbox(on)
    clear_outside(off, bo)                     # off：裁掉主体外残留
    clear_outside(on, bn)                      # on：辉光保留 pad 范围，外面清掉

    # on 态缩放 + 平移，使主体与 off 主体对齐（同宽、同心）
    ow = bo[2] - bo[0]
    nw = bn[2] - bn[0]
    scale = ow / nw
    on = on.resize((max(1, round(on.width * scale)), max(1, round(on.height * scale))), Image.LANCZOS)
    bn2 = dark_bbox(on)
    dx = round(((bo[0] + bo[2]) - (bn2[0] + bn2[2])) / 2)
    dy = round(((bo[1] + bo[3]) - (bn2[1] + bn2[3])) / 2)
    aligned = Image.new("RGBA", off.size, (0, 0, 0, 0))
    aligned.paste(on, (dx, dy), on)
    on = aligned

    # 统一缩放：主体宽 → BODY_W，两态一起（保持相对关系）
    k = BODY_W / ow
    tw, th = round(off.width * k), round(off.height * k)
    off = off.resize((tw, th), Image.LANCZOS)
    on  = on.resize((tw, th), Image.LANCZOS)
    bo2 = dark_bbox(off)
    cx_body = (bo2[0] + bo2[2]) / 2
    cy_body = (bo2[1] + bo2[3]) / 2

    def compose(im):
        c = Image.new("RGBA", CANVAS, (0, 0, 0, 0))
        c.paste(im, (round(CANVAS[0]/2 - cx_body), round(CANVAS[1]/2 - cy_body)), im)
        return c

    off_c, on_c = compose(off), compose(on)
    off_c.save(os.path.join(OUT_DIR, f"btn_{name}_off.png"))
    on_c.save(os.path.join(OUT_DIR, f"btn_{name}_on.png"))
    bw, bh = bo2[2]-bo2[0], bo2[3]-bo2[1]
    print(f"{name}: body {bw}x{bh} aspect {round(bw/bh,3)} canvas {CANVAS}")
    tiles.append((name, off_c, on_c))

pw, ph = CANVAS[0], CANVAS[1]
pv = Image.new("RGBA", (pw * 4 + 50, ph * 2 + 30), (245, 246, 248, 255))
for i, (name, off_c, on_c) in enumerate(tiles):
    pv.paste(off_c, (10 + i * (pw + 10), 10), off_c)
    pv.paste(on_c,  (10 + i * (pw + 10), ph + 20), on_c)
pv.save(os.path.join(os.path.dirname(os.path.abspath(__file__)), "preview_btn_unified.png"))
print("done")
