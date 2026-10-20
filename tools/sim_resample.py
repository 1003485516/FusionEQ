# 重采样对比：模拟 C++ renderSprite（Catmull-Rom + 2x2 子采样）vs 旧双线性路径
# 用法: python sim_resample.py <png> <输出像素> <角度(度)> <输出文件>
import sys
import numpy as np
from PIL import Image

def mipmap(im):
    # 对应 sslKnobTex：逐级 2:1 缩到 <=320
    while im.width > 320:
        im = im.resize((im.width // 2, im.height // 2), Image.LANCZOS)
    return im

def catmull(t):
    t = np.abs(t)
    return np.where(t < 1.0, 1.5*t**3 - 2.5*t**2 + 1.0,
           np.where(t < 2.0, -0.5*t**3 + 2.5*t**2 - 4.0*t + 2.0, 0.0))

def render_hq(src, out_side, angle):
    a = np.asarray(src).astype(np.float64)
    sh, sw = a.shape[:2]
    cs, sn = np.cos(angle), np.sin(angle)
    half = out_side * 0.5
    spo = sw / out_side
    out = np.zeros((out_side, out_side, 4), np.float64)
    for y in range(out_side):
        py = y + 0.5 - half
        for x in range(out_side):
            px = x + 0.5 - half
            acc = np.zeros(4)
            for s in range(4):
                ox = px + (0.25 if s & 1 else -0.25)
                oy = py + (0.25 if s & 2 else -0.25)
                rx =  ox*cs + oy*sn
                ry = -ox*sn + oy*cs
                u = sw*0.5 + rx*spo
                v = sh*0.5 + ry*spo
                if u < -1 or v < -1 or u > sw or v > sh:
                    continue
                iu = min(max(int(np.floor(u)), 1), max(1, sw-3))
                iv = min(max(int(np.floor(v)), 1), max(1, sh-3))
                wx = catmull((u-iu) - (np.arange(4)-1))
                wy = catmull((v-iv) - (np.arange(4)-1))
                for j in range(4):
                    if wy[j] == 0: continue
                    for i in range(4):
                        w = wx[i]*wy[j]
                        if w == 0: continue
                        acc += w * a[iv+j-1, iu+i-1, :]
            out[y, x] = acc * 0.25
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8), "RGBA")

def render_old(src, out_side, angle):
    big = out_side * 2
    t = src.convert("RGBA").resize((big, big), Image.BILINEAR)
    t = t.rotate(-np.degrees(angle), resample=Image.BILINEAR, center=(big/2, big/2))
    return t.resize((out_side, out_side), Image.BILINEAR)

if __name__ == "__main__":
    path  = sys.argv[1] if len(sys.argv) > 1 else "Resources/knob_pultec.png"
    out   = int(sys.argv[2]) if len(sys.argv) > 2 else 130
    deg   = float(sys.argv[3]) if len(sys.argv) > 3 else 35.0
    dst   = sys.argv[4] if len(sys.argv) > 4 else "preview_resample.png"

    im = mipmap(Image.open(path).convert("RGBA"))
    hq, old = render_hq(im, out, np.radians(deg)), render_old(im, out, np.radians(deg))

    def panel(img):
        z = img.resize((out*4, out*4), Image.NEAREST)
        bg = Image.new("RGB", (out*4, out*4), (238, 242, 246))
        bg.paste(z, (0, 0), z)
        return bg

    a, b = panel(old), panel(hq)
    canvas = Image.new("RGB", (a.width*2+16, a.height), (255, 255, 255))
    canvas.paste(a, (0, 0)); canvas.paste(b, (a.width+16, 0))
    canvas.save(dst)
    print("mipmap", im.size, "-> out", out, "saved", dst)
