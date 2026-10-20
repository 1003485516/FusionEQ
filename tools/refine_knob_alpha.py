# -*- coding: utf-8 -*-
"""旋钮素材边缘精修（第二轮，从原图 tools/backup_knobs 重做）：
· 圆盘钮（knob_manley）：解析拟合圆心/半径，alpha 用 smoothstep 重建 ——
  完美抗锯齿圆边，照片抠图的灰色残渣环整体消失；
· 非圆钮（银裙/舌针等）：3×3 中值 ×2 去残渣斑点 + 高斯平滑 +
  smoothstep 重整 1.5px 过渡带；
· 所有钮：外圈 2.5px 的 RGB 向内部扩散色混合，清除抠图残留的背景污染。"""
from PIL import Image, ImageFilter
import numpy as np, os, shutil

RES = os.path.join(os.path.dirname(__file__), "..", "Resources")
BAK = os.path.join(os.path.dirname(__file__), "backup_knobs")

DISC = {"knob_manley"}
OTHER = ["knob_api215", "knob_api_air", "knob_api_freq", "knob_api_gain",
         "knob_bandwidth", "knob_blue", "knob_blue_glass", "knob_red"]


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def diffuse_band(rgb, core, iters=100):
    """ring 区 RGB ← core 区扩散色。"""
    f = rgb.astype(np.float64) * core[:, :, None]
    w = core.astype(np.float64)
    for _ in range(iters):
        fs = np.zeros_like(f); ws = np.zeros_like(w)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                fs += np.roll(np.roll(f, dy, 0), dx, 1)
                ws += np.roll(np.roll(w, dy, 0), dx, 1)
        ok = ws > 0
        new = np.where(ok[:, :, None], fs / np.maximum(ws, 1e-9)[:, :, None], f)
        f = np.where(core[:, :, None], f, new)
        w = np.where(core, w, ok.astype(np.float64))
    return f


def defringe(rgb, a_new, full, inner_px=2.5):
    """full = 完整不透明掩膜；距边缘 inner_px 内的 RGB 向扩散色 ramp 混合。"""
    import scipy.ndimage as _  # noqa  (不依赖 scipy，用手工距离)
    # 简易距离：对 full 做腐蚀近似 —— 用 alpha 高斯差
    core = full.copy()
    er = Image.fromarray((full * 255).astype(np.uint8), "L") \
              .filter(ImageFilter.MinFilter(7))          # ≈ 腐蚀 3px
    core = np.array(er) > 128
    fill = diffuse_band(rgb, core)
    edge = full & ~core                                   # 外圈 ~3px 环
    er2 = Image.fromarray((edge * 255).astype(np.uint8), "L") \
               .filter(ImageFilter.MaxFilter(5))
    band = (np.array(er2) > 128) & full                   # 边环再向内扩 2px
    # 半透明像素：完全用扩散色（清背景残留）；不透明环内像素：55% 混合（保纹理）
    w = np.where(a_new < 250, 1.0, 0.55)[:, :, None]
    out = rgb.astype(np.float64) * (1 - w) + fill * w
    return np.where(band[:, :, None], out, rgb.astype(np.float64))


def process_disc(path, bak_path):
    im = Image.open(bak_path).convert("RGBA")
    arr = np.array(im)
    a0 = arr[:, :, 3]
    mask = a0 >= 128

    ys, xs = np.nonzero(mask)
    cy, cx = ys.mean(), xs.mean()
    d = np.hypot(np.arange(arr.shape[1])[None, :] - cx,
                 np.arange(arr.shape[0])[:, None] - cy)
    r_mean = np.sqrt(mask.sum() / np.pi)
    # 圆度检查：95% 分位半径偏离均值 <4% 才按圆盘处理
    r95 = np.percentile(d[mask], 95)
    if abs(r95 - r_mean) / r_mean > 0.04:
        return False

    R = r_mean + 0.6
    # alpha = 圆内 255，圆缘 1.4px smoothstep 过渡，圆外 0（此前公式写反了）
    a_new = ((1.0 - smoothstep(R - 1.1, R + 0.3, d)) * 255).astype(np.uint8)
    a_new[d < R - 1.1] = 255
    a_new[d > R + 0.3] = 0

    # 外圈 RGB 重铺：用 d < R-10 的干净内部做扩散色 —— 清除照片裙边残渣
    core_clean = d < (R - 10.0)
    fill = diffuse_band(arr[:, :, :3], core_clean)
    rgb = np.where((d >= R - 10.0)[:, :, None],
                   np.clip(fill, 0, 255), arr[:, :, :3].astype(np.float64))
    out = np.dstack([np.clip(rgb, 0, 255).astype(np.uint8), a_new])
    Image.fromarray(out, "RGBA").save(path)
    return True


def process_other(path, bak_path):
    im = Image.open(bak_path).convert("RGBA")
    arr = np.array(im)
    a0 = arr[:, :, 3]
    mask = a0 >= 128

    a_im = Image.fromarray(a0, "L")
    for _ in range(2):
        a_im = a_im.filter(ImageFilter.MedianFilter(3))
    a_im = a_im.filter(ImageFilter.GaussianBlur(1.0))
    a_f = np.array(a_im).astype(np.float64)
    a_new = (smoothstep(40.0, 215.0, a_f) * 255).astype(np.uint8)

    rgb = defringe(arr[:, :, :3], a_new, mask)
    out = np.dstack([np.clip(rgb, 0, 255).astype(np.uint8), a_new])
    Image.fromarray(out, "RGBA").save(path)


for name in DISC:
    p, b = os.path.join(RES, name + ".png"), os.path.join(BAK, name + ".png")
    ok = process_disc(p, b)
    print(name, "-> analytic circle" if ok else "-> NOT disc, fallback")
    if not ok:
        process_other(p, b)
for name in OTHER:
    p, b = os.path.join(RES, name + ".png"), os.path.join(BAK, name + ".png")
    process_other(p, b)
    print(name, "-> median+smoothstep")
print("done")
