# -*- coding: utf-8 -*-
"""API 硬件按钮（硬朗模式四段）：复用 HP 按钮素材（已统一画布），
抹掉「HP」文字（行插值修复背景渐变）→ 居中重绘段位文字（颜色取自原文字）→
Resources/btn_lf|lmf|hmf|hf_off|on.png
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
                        round(left[2]  + (right[2]  - left[2])  * k),
                        255)
    return im

# 三字母宽度参考：AIR 重绘时高度约取原文字 0.9 倍（同 make_air_button）
tiles = {}
for state in ("off", "on"):
    base = Image.open(os.path.join(RES, f"btn_hp_{state}.png")).convert("RGBA")
    box, col = text_bbox(base, 145, 335, 55, 155)
    box = (box[0] - 2, box[1] - 2, box[2] + 2, box[3] + 2)
    print(state, "text bbox:", box, "color:", col)
    inpaint_rows(base, box)

    for label in ("LF", "LMF", "HMF", "HF"):
        im = base.copy()
        th = box[3] - box[1]
        size = int(th * 1.12)
        font = None
        for fp in (r"C:\Windows\Fonts\arialbd.ttf", r"C:\Windows\Fonts\segoeuib.ttf",
                   r"C:\Windows\Fonts\tahoma.ttf"):
            if os.path.exists(fp):
                font = ImageFont.truetype(fp, size)
                break
        d = ImageDraw.Draw(im)
        tb = d.textbbox((0, 0), label, font=font)
        # 三字母（LMF/HMF）过宽则缩字号；不超过原文字宽的 1.5 倍
        while tb[2] - tb[0] > (box[2] - box[0]) * 1.5 and size > 20:
            size -= 2
            font = ImageFont.truetype(font.path, size)
            tb = d.textbbox((0, 0), label, font=font)
        cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
        d.text((cx - (tb[2] - tb[0]) / 2 - tb[0], cy - (tb[3] - tb[1]) / 2 - tb[1]),
               label, font=font, fill=col)
        im.save(os.path.join(RES, f"btn_{label.lower()}_{state}.png"))
        tiles[(label, state)] = im
        print(label, state, "saved, font size", size)

# 预览：4 对并排（off / on 交替）
w, h = tiles[("LF", "off")].size
pv = Image.new("RGBA", ((w + 10) * 8 + 10, h + 20), (60, 70, 77, 255))
for i, label in enumerate(("LF", "LMF", "HMF", "HF")):
    pv.paste(tiles[(label, "off")], (10 + i * 2 * (w + 10), 10), tiles[(label, "off")])
    pv.paste(tiles[(label, "on")],  (10 + i * 2 * (w + 10) + w + 10, 10), tiles[(label, "on")])
pv.save(os.path.join(os.path.dirname(os.path.abspath(__file__)), "preview_btn_api.png"))
print("done")
