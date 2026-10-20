# -*- coding: utf-8 -*-
"""AIR 硬件按钮：复用 HP 按钮素材（已统一画布），
抹掉「HP」文字（行插值修复背景渐变）→ 居中重绘「AIR」（颜色取自原文字）→
Resources/btn_air_off|on.png
"""
from PIL import Image, ImageDraw, ImageFont
import os

RES = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Resources")

def text_bbox(im, x0, x1, y0, y1, lum_max=130):
    """在中央区域找深棕色文字像素的包围盒 + 平均色
    （文字是棕色 r-b 明显；边框/阴影是近灰黑 r≈b → 排除）"""
    px = im.load()
    l, t, r, b = x1, y1, x0, y0
    n = 0; sr = sg = sb = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            rr, gg, bb, aa = px[x, y]
            if (aa > 40 and (rr + gg + bb) / 3.0 < lum_max
                    and rr - bb > 16 and rr - gg > 6):
                l = min(l, x); t = min(t, y); r = max(r, x); b = max(b, y)
                n += 1; sr += rr; sg += gg; sb += bb
    col = (sr // max(1, n), sg // max(1, n), sb // max(1, n)) if n else (70, 50, 35)
    return (l, t, r, b), col

def inpaint_rows(im, box, pad=3):
    """文字区域逐行水平插值填充（保留背景渐变）"""
    l, t, r, b = box
    px = im.load()
    for y in range(max(0, t - 1), min(im.height, b + 2)):
        # 行两侧取样（跳过文字影响区，向外取 pad 像素的平均）
        def samp(xs):
            rr = gg = bb = n = 0
            for x in xs:
                c = px[x, y]
                rr += c[0]; gg += c[1]; bb += c[2]; n += 1
            return (rr // n, gg // n, bb // n)
        left  = samp(range(max(0, l - 14), max(1, l - 4)))
        right = samp(range(min(im.width - 1, r + 4), min(im.width, r + 14)))
        w = max(1, r - l)
        for i, x in enumerate(range(l, r)):
            k = i / w
            px[x, y] = (round(left[0]  + (right[0]  - left[0])  * k),
                        round(left[1]  + (right[1]  - left[1])  * k),
                        round(left[2]  + (right[2]  - left[2])  * k, ),
                        255)
    return im

tiles = []
for state in ("off", "on"):
    im = Image.open(os.path.join(RES, f"btn_hp_{state}.png")).convert("RGBA")
    # 文字搜索区 = 主体内部中央（画布 480x208，主体居中 440 宽），
    # 扩 2px 余量 + inpaint pad 覆盖抗锯齿边缘
    box, col = text_bbox(im, 145, 335, 55, 155)
    box = (box[0] - 2, box[1] - 2, box[2] + 2, box[3] + 2)
    print(state, "text bbox:", box, "color:", col)
    inpaint_rows(im, box)

    # 重绘 AIR（与原文字同色、近同高、粗体）
    th = box[3] - box[1]
    size = int(th * 1.12)
    font = None
    for fp in (r"C:\Windows\Fonts\arialbd.ttf", r"C:\Windows\Fonts\segoeuib.ttf",
               r"C:\Windows\Fonts\tahoma.ttf"):
        if os.path.exists(fp):
            font = ImageFont.truetype(fp, size)
            break
    d = ImageDraw.Draw(im)
    tb = d.textbbox((0, 0), "AIR", font=font)
    # 收缩到不超过原文字宽太多（原 HP 两字母较窄，AIR 三字母取 0.9 倍高度微调）
    while tb[2] - tb[0] > (box[2] - box[0]) * 1.45 and size > 20:
        size -= 2
        font = ImageFont.truetype(font.path, size)
        tb = d.textbbox((0, 0), "AIR", font=font)
    cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
    d.text((cx - (tb[2] - tb[0]) / 2 - tb[0], cy - (tb[3] - tb[1]) / 2 - tb[1]),
           "AIR", font=font, fill=col)
    im.save(os.path.join(RES, f"btn_air_{state}.png"))
    tiles.append(im)
    print(state, "saved, font size", size)

# 预览
w, h = tiles[0].size
pv = Image.new("RGBA", (w * 2 + 30, h + 20), (60, 70, 77, 255))
pv.paste(tiles[0], (10, 10), tiles[0])
pv.paste(tiles[1], (w + 20, 10), tiles[1])
pv.save(os.path.join(os.path.dirname(os.path.abspath(__file__)), "preview_btn_air.png"))
print("done")
