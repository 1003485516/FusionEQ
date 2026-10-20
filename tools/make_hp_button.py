# -*- coding: utf-8 -*-
"""HP 硬件开关素材处理：
棋盘格底 JPG → 边缘洪泛去底（含投影）→ 两态统一画布裁剪 → Resources/btn_hp_off|on.png
"""
from PIL import Image
from collections import deque
import os

SRC_OFF = r"D:\微信\xwechat_files\lingchen1003_bce1\temp\RWTemp\2026-10\9e20f478899dc29eb19741386f9343c8\e2374d4458ad3b564b1eea34ae84e721.jpg"
SRC_ON  = r"D:\微信\xwechat_files\lingchen1003_bce1\temp\RWTemp\2026-10\9e20f478899dc29eb19741386f9343c8\5fbc750bd9cba208747cdc3279e333a7.jpg"
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
        # 棋盘格白/灰 + 浅灰投影：亮且低饱和
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

def bbox_of(im):
    return im.getbbox()  # 基于 alpha

off = kill_bg(SRC_OFF)
on  = kill_bg(SRC_ON)

bo, bn = dark_bbox(off), dark_bbox(on)
l = min(bo[0], bn[0]); t = min(bo[1], bn[1])
r = max(bo[2], bn[2]); b = max(bo[3], bn[3])
# 统一画布（两态同尺寸，切换时不跳动），四周留 3px 余量（含一点自然投影）
l = max(0, l - 3); t = max(0, t - 3)
r = min(off.width,  r + 3); b = min(off.height, b + 3)

# 裁掉深色主体包围盒之外的残留投影
for im in (off, on):
    pxx = im.load()
    for y in range(im.height):
        for x in range(im.width):
            if x < l or y < t or x >= r or y >= b:
                pxx[x, y] = (0, 0, 0, 0)

off = off.crop((l, t, r, b))
on  = on.crop((l, t, r, b))

# 缩到工作尺寸（宽 480，足够清晰）
tw = 480
th = round(off.height * tw / off.width)
off = off.resize((tw, th), Image.LANCZOS)
on  = on.resize((tw, th), Image.LANCZOS)

os.makedirs(OUT_DIR, exist_ok=True)
off.save(os.path.join(OUT_DIR, "btn_hp_off.png"))
on.save(os.path.join(OUT_DIR, "btn_hp_on.png"))
print("size:", off.size, " aspect:", round(tw / th, 3))

# 预览拼图
pv = Image.new("RGBA", (tw * 2 + 30, th + 20), (245, 246, 248, 255))
pv.paste(off, (10, 10), off)
pv.paste(on, (tw + 20, 10), on)
pv.save(os.path.join(os.path.dirname(os.path.abspath(__file__)), "preview_btn_hp.png"))
print("done")
