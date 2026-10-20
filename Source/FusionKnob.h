/* =============================================================================
   Fusion EQ  ·  FusionKnob.h
   旋钮外观与交互（竖直拖动 / 滚轮 / 方向键 / 双击复位）

   ⚠ 参数映射说明（重要）：
   这里**不用** SliderAttachment —— 它要求 Slider 直接持有参数的“真实值”，
   而我们把 Slider 固定在 0..1 的归一化行程上。两者混用会让旋钮只改到
   参数范围的前 1 dB（这正是之前 OUTPUT 旋钮“没用”的原因）。
   所以改成手写双向绑定：slider 值 = 归一化位置，参数值 = convertFrom0to1(slider)。
   ========================================================================== */
#pragma once
#include <JuceHeader.h>
#include "BinaryData.h"

namespace Fusion
{

/* 旋钮行程（角度）：0 = 12 点，顺时针为正 */
struct Sweep
{
    float startDeg = -135.0f;
    float endDeg   =  135.0f;
};

inline Sweep standardSweep() noexcept { return Sweep {}; }

/* AIR 专用：八点钟起，顺时针 300°，止于六点钟 */
inline Sweep airSweep() noexcept { return Sweep { -120.0f, 180.0f }; }

/* ---------------------------------------------------------------------------
   旋钮外观
--------------------------------------------------------------------------- */
class KnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KnobLookAndFeel()
    {
        setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff0e9bd6));
    }

    /* ---- 缓存纹理：金属噪点 / 定向拉丝（懒加载，UI 线程专用） ---- */
    static const juce::Image& noiseTile()
    {
        static juce::Image img = []
        {
            juce::Image im (juce::Image::ARGB, 128, 128, true);
            juce::Image::BitmapData bd (im, juce::Image::BitmapData::writeOnly);
            juce::Random rng;
            for (int y = 0; y < 128; ++y)
                for (int x = 0; x < 128; ++x)
                {
                    const auto v = (juce::uint8) (120 + rng.nextInt (110));
                    bd.setPixelColour (x, y, juce::Colour (v, v, v, (juce::uint8) rng.nextInt (60)));
                }
            return im;
        }();
        return img;
    }

    static const juce::Image& streakTile()
    {
        static juce::Image img = []
        {
            juce::Image im (juce::Image::ARGB, 128, 128, true);
            juce::Image::BitmapData bd (im, juce::Image::BitmapData::writeOnly);
            juce::Random rng;
            for (int y = 0; y < 128; ++y)
            {
                const auto rowV = 110 + rng.nextInt (130);
                for (int x = 0; x < 128; ++x)
                {
                    const auto v = (juce::uint8) juce::jlimit (60, 230, rowV + rng.nextInt (26) - 13);
                    bd.setPixelColour (x, y, juce::Colour (v, v, v, (juce::uint8) (rng.nextInt (34) + 8)));
                }
            }
            return im;
        }();
        return img;
    }

    /* 贴图库：编译期嵌入的实拍质感控件（0-3 SSL 四色钮 / 4 Manley / 5 Pultec 滚花钮
       / 6 Pultec 长方拨杆 / 7 Pultec 指针拨杆 / 8 BANDWIDTH 带宽钮 / 9 AIR 白瓷钮
       / 10 SSL 红钮 LP/HP / 11 Neve 米色钮 / 12 蓝色步进钮
       / 13 API 银钮·FREQ / 14 API 蓝钮·GAIN / 15 红色玻璃环钮·AIR 厚实
       / 16 蓝色玻璃环钮·厚实 LOW/MID 频点 / 17 橙芯银裙钮·215L 滤波）。旋钮与拨杆控件共用。 */
    static const juce::Image& sslKnobTex (int idx)
    {
        struct Holder { juce::Image imgs[19]; };
        static const Holder holder = []
        {
            Holder h;
            const struct { const char* data; int size; } src[19] = {
                { BinaryData::knob_ssl_black_png,  BinaryData::knob_ssl_black_pngSize  },
                { BinaryData::knob_ssl_green_png,  BinaryData::knob_ssl_green_pngSize  },
                { BinaryData::knob_ssl_blue_png,   BinaryData::knob_ssl_blue_pngSize   },
                { BinaryData::knob_ssl_purple_png, BinaryData::knob_ssl_purple_pngSize },
                { BinaryData::knob_manley_png,     BinaryData::knob_manley_pngSize     },
                { BinaryData::knob_pultec_png,     BinaryData::knob_pultec_pngSize     },
                { BinaryData::knob_lever_png,      BinaryData::knob_lever_pngSize      },
                { BinaryData::knob_atten_png,      BinaryData::knob_atten_pngSize      },
                { BinaryData::knob_bandwidth_png,  BinaryData::knob_bandwidth_pngSize  },
                { BinaryData::knob_air_png,        BinaryData::knob_air_pngSize        },
                { BinaryData::knob_ssl_red_png,    BinaryData::knob_ssl_red_pngSize    },
                { BinaryData::knob_neve_png,       BinaryData::knob_neve_pngSize       },
                { BinaryData::knob_blue_png,       BinaryData::knob_blue_pngSize       },
                { BinaryData::knob_api_freq_png,   BinaryData::knob_api_freq_pngSize   },
                { BinaryData::knob_api_gain_png,   BinaryData::knob_api_gain_pngSize   },
                { BinaryData::knob_red_png,        BinaryData::knob_red_pngSize        },
                { BinaryData::knob_blue_glass_png, BinaryData::knob_blue_glass_pngSize },
                { BinaryData::knob_api215_png,     BinaryData::knob_api215_pngSize     },
                { BinaryData::knob_api_air_png,    BinaryData::knob_api_air_pngSize    }
            };

            for (int i = 0; i < 19; ++i)
            {
                auto full = juce::ImageFileFormat::loadFrom (src[i].data, (size_t) src[i].size);

                /* 渐进式半分辨率链（等效 mipmap）：JUCE 软件渲染只有双线性插值，
                   1000px 直接缩到 130px（8:1）会发糊；先逐级 2:1 缩到显示尺寸的
                   2 倍左右再旋转，清晰度接近 Lanczos 预览的效果 */
                juce::Image cur = full;
                while (cur.getWidth() > 320)
                    cur = cur.rescaled (cur.getWidth() / 2, cur.getHeight() / 2,
                                        juce::Graphics::highResamplingQuality);

                h.imgs[i] = cur;
            }

            return h;
        }();

        return holder.imgs[juce::jlimit (0, 18, idx)];
    }

    /* EQP 风格机架面板（温润模式三段式区域打底，编译期嵌入） */
    static const juce::Image& pultecPanel()
    {
        static const juce::Image img = juce::ImageFileFormat::loadFrom (
            BinaryData::panel_eqp_png, (size_t) BinaryData::panel_eqp_pngSize);
        return img;
    }

    /* ── 高质量旋转渲染 ──────────────────────────────────────────────
       JUCE 软件渲染的 drawImageTransformed 只有双线性插值，旋转后的旋钮边缘、
       滚花纹理与金属高光都会发虚。这里自建重采样：
         · Catmull-Rom 双三次卷积（4×4 权重，负瓣让边缘重新锐利）
         · 每像素 2×2 子采样（旋转边界抗锯齿）
         · 源包围盒外像素直接跳过
         · 角度 0.5° 量化 + 精灵缓存（拖动时同一档位复用已渲染位图，
           卷积开销从"每帧一次"降为"每 0.5° 一次"）                     */

    /* 通用版：以 (pivSrc) 为旋转中心、按 srcPerOut（每个输出像素对应的源像素数）
       缩放、旋转 angleRad，渲染成 outW×outH 位图；(pivOut) 为该中心在输出图中的位置。 */
    static juce::Image renderSprite (const juce::Image& src, int outW, int outH,
                                     float pivOutX, float pivOutY,
                                     float pivSrcX, float pivSrcY,
                                     float srcPerOut, float angleRad)
    {
        juce::Image dst (juce::Image::ARGB, outW, outH, true);

        const juce::Image::BitmapData sd (src, juce::Image::BitmapData::readOnly);
        const int   sw = sd.width;
        const int   sh = sd.height;
        const float cs = std::cos (angleRad);
        const float sn = std::sin (angleRad);

        auto catmull = [] (float t)
        {
            t = std::abs (t);
            if (t < 1.0f) return 1.5f * t * t * t - 2.5f * t * t + 1.0f;
            if (t < 2.0f) return -0.5f * t * t * t + 2.5f * t * t - 4.0f * t + 2.0f;
            return 0.0f;
        };

        juce::Image::BitmapData dd (dst, juce::Image::BitmapData::writeOnly);

        for (int y = 0; y < outH; ++y)
        {
            for (int x = 0; x < outW; ++x)
            {
                const float px = (float) x + 0.5f - pivOutX;
                const float py = (float) y + 0.5f - pivOutY;

                float acc[4] { 0.0f, 0.0f, 0.0f, 0.0f };

                for (int s = 0; s < 4; ++s)        // 2×2 子采样
                {
                    const float ox = px + ((s & 1) ? 0.25f : -0.25f);
                    const float oy = py + ((s & 2) ? 0.25f : -0.25f);

                    /* 反向旋转：输出坐标 → 源图坐标 */
                    const float rx = ox * cs + oy * sn;
                    const float ry = -ox * sn + oy * cs;
                    const float u  = pivSrcX + rx * srcPerOut;
                    const float v  = pivSrcY + ry * srcPerOut;

                    /* 完全落在源图之外 → 该子采样贡献 0（留 1px 余量保住边缘） */
                    if (u < -1.0f || v < -1.0f || u > (float) sw || v > (float) sh)
                        continue;

                    /* 采样窗口夹在合法范围内（省掉每 tap 的边界判断） */
                    const int   iu = juce::jlimit (1, juce::jmax (1, sw - 3), (int) std::floor (u));
                    const int   iv = juce::jlimit (1, juce::jmax (1, sh - 3), (int) std::floor (v));
                    const float fu = u - (float) iu;
                    const float fv = v - (float) iv;

                    float wx[4], wy[4];
                    for (int k = 0; k < 4; ++k)
                    {
                        wx[k] = catmull (fu - (float) (k - 1));
                        wy[k] = catmull (fv - (float) (k - 1));
                    }

                    for (int j = 0; j < 4; ++j)
                    {
                        const float wyj = wy[j];
                        if (wyj == 0.0f) continue;

                        for (int i = 0; i < 4; ++i)
                        {
                            const float w = wx[i] * wyj;
                            if (w == 0.0f) continue;

                            const auto* tp = sd.getPixelPointer (iu + i - 1, iv + j - 1);  // BGRA
                            acc[0] += w * (float) tp[0];
                            acc[1] += w * (float) tp[1];
                            acc[2] += w * (float) tp[2];
                            acc[3] += w * (float) tp[3];
                        }
                    }
                }

                auto* p = dd.getPixelPointer (x, y);
                for (int c = 0; c < 4; ++c)
                    p[c] = (juce::uint8) juce::jlimit (0.0f, 255.0f, acc[c] * 0.25f);
            }
        }

        return dst;
    }

    /* 带缓存的通用入口：同一 (贴图, 输出尺寸, 缩放比, 0.5° 角度档) 只渲染一次。
       LRU + 字节预算：超出预算淘汰最久未用的精灵，而不是整批清空（避免拖动中周期性卡顿）。 */
    static juce::Image spriteFor (const juce::Image& src, int texId,
                                  int outW, int outH,
                                  float pivOutX, float pivOutY,
                                  float pivSrcX, float pivSrcY,
                                  float srcPerOut, float angleRad)
    {
        constexpr float kPi = juce::MathConstants<float>::pi;

        const int   bucket  = juce::roundToInt (angleRad * (180.0f / kPi) * 2.0f);  // 0.5° 一档
        const float snapped = (float) bucket * 0.5f * (kPi / 180.0f);

        struct Cache
        {
            juce::HashMap<juce::int64, juce::Image> map;
            juce::HashMap<juce::int64, int>         sizes;   // 每个精灵的字节数
            juce::Array<juce::int64> lru;                    // lru[0] = 最近使用
            size_t bytes = 0;
        };
        static Cache c;
        constexpr size_t budget = 24u << 20;                          // 24 MB 缓存预算

        const juce::int64 key = ((((((juce::int64) (texId + 1) * 2048 + outW) * 2048 + outH) * 8192
                                   + (juce::int64) (srcPerOut * 64.0f)) * 8192) + bucket + 4096);

        if (c.map.contains (key))
        {
            const int i = c.lru.indexOf (key);
            if (i > 0) { c.lru.remove (i); c.lru.insert (0, key); }   // 提到队首
        }
        else
        {
            const int sz = outW * outH * 4;
            c.map.set (key, renderSprite (src, outW, outH, pivOutX, pivOutY,
                                          pivSrcX, pivSrcY, srcPerOut, snapped));
            c.sizes.set (key, sz);
            c.lru.insert (0, key);
            c.bytes += (size_t) sz;

            while (c.bytes > budget && c.lru.size() > 1)
            {
                const juce::int64 victim = c.lru.getLast();
                const int        vsz    = c.sizes[victim];

                c.map.remove (victim);
                c.sizes.remove (victim);
                c.lru.removeLast();
                c.bytes = (c.bytes >= (size_t) vsz) ? (c.bytes - (size_t) vsz) : 0;
            }
        }

        return c.map[key];
    }

    /* 旋钮入口：贴图绕图心旋转，输出 side×side，绘制中心 (cx, cy) */
    static void drawRotatedHQ (juce::Graphics& g, const juce::Image& src, int texId,
                               float cx, float cy, float side, float angleRad)
    {
        if (! src.isValid() || side < 4.0f)
            return;

        const int outSide = juce::jmax (8, juce::roundToInt (side));

        const auto spr = spriteFor (src, texId, outSide, outSide,
                                    (float) outSide * 0.5f, (float) outSide * 0.5f,
                                    (float) src.getWidth()  * 0.5f,
                                    (float) src.getHeight() * 0.5f,
                                    (float) src.getWidth() / (float) outSide,
                                    angleRad);

        g.drawImageAt (spr,
                       juce::roundToInt (cx - side * 0.5f),
                       juce::roundToInt (cy - side * 0.5f),
                       false);
    }

    /* 拨杆入口：绕 (pivotOut) 处的底座旋转，输出正好铺满组件 (0,0,w,h) */
    static void drawPivotedHQ (juce::Graphics& g, const juce::Image& src, int texId,
                               int w, int h, float pivotOutX, float pivotOutY,
                               float pivotSrcX, float pivotSrcY, float srcPerOut,
                               float angleRad)
    {
        if (! src.isValid() || w < 4 || h < 4)
            return;

        const auto spr = spriteFor (src, texId, w, h, pivotOutX, pivotOutY,
                                    pivotSrcX, pivotSrcY, srcPerOut, angleRad);

        g.drawImageAt (spr, 0, 0, false);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto cx     = bounds.getCentreX();
        const auto cy     = bounds.getCentreY();

        const bool bipolar = (int) slider.getProperties() ["bipolar"] == 1;
        const bool silver  = (int) slider.getProperties() ["silver"] == 1;
        const bool tube    = (int) slider.getProperties() ["tube"]   == 1;
        const int  tex     = (int) slider.getProperties().getWithDefault ("tex", -1);
        const auto accent  = slider.findColour (juce::Slider::rotarySliderFillColourId);

        /* 电子管"热度"：行程越大灯丝越亮（双极旋钮从中心 0 起） */
        const float heat = tube ? juce::jlimit (0.0f, 1.0f,
            bipolar ? std::abs (sliderPos - 0.5f) * 2.0f : sliderPos) : 0.0f;

        const float a0    = rotaryStartAngle;
        const float a1    = rotaryEndAngle;
        const float angle = a0 + sliderPos * (a1 - a0);

        const auto rimR   = radius - 0.5f;
        const auto bodyR  = rimR - 6.2f;
        const auto trackR = radius - 2.5f;

        /* 1 · 底座环境阴影（旋钮"浮"在面板上） */
        {
            const auto shR = radius + 5.0f;
            juce::ColourGradient sh (juce::Colour (0x4d141a24), cx, cy + 1.0f,
                                     juce::Colour (0x00141a24), cx, cy + 1.0f, true);
            g.setGradientFill (sh);
            g.fillEllipse (cx - shR, cy - shR + 3.0f, shR * 2.0f, shR * 2.0f);
        }

        /* 1.5 · 电子管环境辉光：随行程点亮的暖光晕，像管子照亮面板 */
        if (tube)
        {
            const auto hR = radius + 7.0f;
            juce::ColourGradient hg (accent.withAlpha (0.05f + 0.13f * heat), cx, cy,
                                     accent.withAlpha (0.0f), cx, cy, true);
            g.setGradientFill (hg);
            g.fillEllipse (cx - hR, cy - hR, hR * 2.0f, hR * 2.0f);
        }

        /* 贴图本体：图内白色长槽/白线即指针，整体随角度旋转。
           texOffset：图内指针朝向补偿——SSL 四色与 Manley 朝 12 点（0），Pultec 滚花钮偏左约 8°。 */
        if (tex >= 0)
        {
            static constexpr float texOffset[19] = { 0.0471f, 0.0471f, 0.0471f, 0.0471f, 0.0f, 2.5010f, 0.0f, 0.0f, 2.5136f, 0.0471f, -0.1819f, 3.1416f, 3.1416f, 2.6480f, 2.6354f, 3.1416f, 3.1416f, 3.5779f, 3.5779f };
            // ↑ tex10（SSL 红钮 LP/HP）槽朝向 +10.4°（实测）→ -0.1819 rad 对齐 12 点
            // ↑ tex0-3（SSL 四色）已换成 AIR 白瓷钮换色版，同一底版 → 同样的 ~2.7° 槽倾斜补偿
            // ↑ tex5（Pultec 齿轮钮）图内指针在 216.7°（圆拟合实测），-216.7° ≡ +143.3° 对齐 12 点
            //   素材已回中：圆盘圆心与画布中心残差 0.1px（修复"拧起来松松垮垮"）
            // ↑ tex8（BANDWIDTH 钮）指针 216° → 补偿 144°
            // ↑ tex9（AIR 白瓷钮）槽理论上指 12 点，AI 生成素材有 ~2.7° 逆时针倾斜 → +0.0471 rad
            // ↑ tex11（Neve 米色钮）白条指针正指 6 点（调试图实测 180°，旧标定 191.5° 有误）→ 补偿 180°
            // ↑ tex12（蓝色步进钮）白条指针正指 6 点（直视素材实测 180°）→ 补偿 180°
            // ↑ tex13（API 银钮·FREQ）舌针 208.3°（成品图上按「超出扇贝尖端」像素实测）→ 补偿 151.7°
            // ↑ tex14（API 蓝钮·GAIN）舌针 209.0°（同上）→ 补偿 151.0°
            // ↑ tex15（红色玻璃环钮·AIR 厚实）白条指针正指 6 点（直视素材 180°）→ 补偿 180°
            // ↑ tex16（蓝色玻璃环钮·厚实频点）白条指针正指 6 点（直视素材 180°）→ 补偿 180°
            // ↑ tex17（215L 橙芯银裙钮）舌针 155°（残余半径突起实测）→ 补偿 205°
            // ↑ tex18（AIR 青芯银裙钮·硬朗）舌针 155°（同管线实测）→ 补偿 205°
            const auto& img  = sslKnobTex (tex);
            /* Manley/Pultec/BANDWIDTH 图盘面留白多（圆盘占图 0.8）→ 2.55 系数让盘面
               完整覆盖本体（露白根因是此前加的容器上限把贴图缩得比本体还小，已撤销）。
               贴图四角透明区被组件裁剪无碍，圆盘本体（2.04·bodyR）始终在组件内。 */
            /* 图盘面留白比例不同 → 各贴图的放大系数（目标：盘面 ≈ 2.04·bodyR）：
               4/5/8 盘占图 0.80 → 2.55；9/10 盘占图 0.846 → 2.41；
               11（Neve）/ 12（蓝步进钮）盘占图 0.90 → 2.27；
               13/14（API 双钮）盘占图 0.781（k=0.869 已含舌针防裁剪缩放）→ 2.27；
               17（215L 橙芯钮）/ 18（AIR 青芯钮）盘占图 0.76（画布含舌针）→ 2.68；
               0-3（瓷面换色版，同 AIR 底版）盘占图 0.846 → 2.45（等视觉盘径换算自旧 2.08/0.995） */
            const float side = bodyR * (tex == 4 || tex == 5 || tex == 8 ? 2.55f
                                        : (tex == 9 || tex == 10) ? 2.41f
                                        : (tex >= 11 && tex <= 14) ? 2.27f
                                        : (tex == 15 || tex == 16) ? 2.32f
                                        : (tex == 17 || tex == 18) ? 2.68f
                                        : tex <= 3 ? 2.45f : 2.08f);

            /* 自建双三次重采样 + 2×2 子采样（带角度精灵缓存），
               替代 JUCE 的双线性 drawImageTransformed —— 这是成品旋钮
               清晰度逼近离线 Lanczos 预览图的关键一层。 */
            drawRotatedHQ (g, img, tex, cx, cy, side, angle + texOffset[tex]);
        }

        if (tex < 0)
        {
        /* 2 · 外圈缎面倒角（上暗-中亮-下深的车削反光；银白版换浅银） */
        {
            if (silver)
            {
                juce::ColourGradient rim (juce::Colour (0xfff6f9fb), cx, cy - rimR,
                                          juce::Colour (0xff6e7880), cx, cy + rimR, false);
                rim.addColour (0.35, juce::Colour (0xffcfd6dc));
                rim.addColour (0.60, juce::Colour (0xffeef2f5));
                g.setGradientFill (rim);
            }
            else
            {
                juce::ColourGradient rim (juce::Colour (0xffc7ced4), cx, cy - rimR,
                                          juce::Colour (0xff3c434a), cx, cy + rimR, false);
                rim.addColour (0.40, juce::Colour (0xff6f777f));
                rim.addColour (0.62, juce::Colour (0xffaeb6bd));
                g.setGradientFill (rim);
            }
            g.fillEllipse (cx - rimR, cy - rimR, rimR * 2.0f, rimR * 2.0f);
        }

        /* 3 · 双面滚花 56 齿（每齿 = 暗面 + 亮面两个四边形，真实立体） */
        {
            const int teeth = 56;
            const auto rOut = rimR - 1.1f, rIn = rimR - 4.6f;
            const auto w = juce::MathConstants<float>::pi / (float) teeth * 0.72f;
            auto pt = [&] (float r, float a)
            {
                return juce::Point<float> (cx + r * std::sin (a), cy - r * std::cos (a));
            };

            for (int i = 0; i < teeth; ++i)
            {
                const auto d = (float) i / (float) teeth * juce::MathConstants<float>::twoPi;

                juce::Path dark;
                dark.addTriangle (pt (rOut, d - w), pt (rOut, d), pt (rIn, d));
                dark.addTriangle (pt (rOut, d - w), pt (rIn, d), pt (rIn, d - w));
                g.setColour (silver ? juce::Colour (0x8c565f66) : juce::Colour (0x8c0e1217));
                g.fillPath (dark);

                juce::Path light;
                light.addTriangle (pt (rOut, d), pt (rOut, d + w), pt (rIn, d + w));
                light.addTriangle (pt (rOut, d), pt (rIn, d + w), pt (rIn, d));
                g.setColour (silver ? juce::Colour (0x78ffffff) : juce::Colour (0x4de6eef5));
                g.fillPath (light);
            }
        }

        /* 4 · 本体：银白阳极铝 / 缎面炭灰 径向渐变（电子管 = 暖色玻壳） */
        {
            if (tube)
            {
                /* 玻壳：深暖炭 + 轻微琥珀内透（电子管玻璃罩） */
                juce::ColourGradient body (juce::Colour (0xff453a2e), cx - bodyR * 0.36f, cy - bodyR * 0.62f,
                                           juce::Colour (0xff1a1510), cx + bodyR * 0.30f, cy + bodyR * 0.85f, true);
                body.addColour (0.50, juce::Colour (0xff2d251c));
                g.setGradientFill (body);
            }
            else if (silver)
            {
                juce::ColourGradient body (juce::Colour (0xffeef2f6), cx - bodyR * 0.36f, cy - bodyR * 0.62f,
                                           juce::Colour (0xff78828b), cx + bodyR * 0.30f, cy + bodyR * 0.85f, true);
                body.addColour (0.45, juce::Colour (0xffc2cad1));
                g.setGradientFill (body);
            }
            else
            {
                juce::ColourGradient body (juce::Colour (0xff71797f), cx - bodyR * 0.36f, cy - bodyR * 0.62f,
                                           juce::Colour (0xff252a2f), cx + bodyR * 0.30f, cy + bodyR * 0.85f, true);
                body.addColour (0.50, juce::Colour (0xff4d545a));
                g.setGradientFill (body);
            }
            g.fillEllipse (cx - bodyR, cy - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        }

        /* 5 · 金属纹理：噪点 + 定向拉丝（缓存图块，裁剪到本体圆内） */
        {
            juce::Path clip;
            clip.addEllipse (cx - bodyR, cy - bodyR, bodyR * 2.0f, bodyR * 2.0f);

            g.saveState();
            g.reduceClipRegion (clip);
            g.setTiledImageFill (noiseTile(), 0, 0, 0.16f);
            g.fillAll();
            g.setTiledImageFill (streakTile(), 0, 0, 0.13f);
            g.fillAll();
            g.restoreState();
        }

        /* 6 · 内凹台阶缝（本体与滚花之间的阴影 + 内侧亮边） */
        g.setColour (juce::Colour (0x8c000000));
        g.drawEllipse (cx - bodyR, cy - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.5f);
        g.setColour (juce::Colour (0x1fffffff));
        g.drawEllipse (cx - bodyR + 1.8f, cy - bodyR + 1.8f,
                       (bodyR - 1.8f) * 2.0f, (bodyR - 1.8f) * 2.0f, 0.7f);

        /* 7 · 顶部柔焦高光（径向白渐变模拟镜头虚化） */
        {
            juce::ColourGradient gloss (juce::Colour (silver ? 0x45ffffff : 0x2bffffff),
                                        cx, cy - bodyR * 0.62f,
                                        juce::Colour (0x00ffffff), cx, cy - bodyR * 0.62f, true);
            gloss.addColour (0.45, juce::Colour (silver ? 0x22ffffff : 0x14ffffff));
            g.setGradientFill (gloss);
            g.fillEllipse (cx - bodyR * 0.90f, cy - bodyR * 1.05f,
                           bodyR * 1.80f, bodyR * 0.95f);
        }

        /* 8 · 底部环境反弹光 */
        {
            juce::ColourGradient bo (juce::Colour (0x1fffffff), cx, cy + bodyR * 0.80f,
                                     juce::Colour (0x00ffffff), cx, cy + bodyR * 0.80f, true);
            g.setGradientFill (bo);
            g.fillEllipse (cx - bodyR * 0.68f, cy + bodyR * 0.42f,
                           bodyR * 1.36f, bodyR * 0.55f);
        }

        }

        if (tex < 0)
        {
        /* 9 · 中央轴帽（凹陷小圆点；电子管 = 发光灯芯） */
        {
            const auto capR = bodyR * 0.10f;
            g.setColour (juce::Colour (0x80000000));
            g.fillEllipse (cx - capR - 1.2f, cy - capR - 0.2f,
                           (capR + 1.2f) * 2.0f, (capR + 1.2f) * 2.0f);

            if (tube)
            {
                /* 灯芯：辉光圈 + 白热核，亮度随热度升高 */
                const auto glowR = capR * (1.6f + 2.2f * heat);
                juce::ColourGradient fg (accent.withAlpha (0.10f + 0.50f * heat), cx, cy,
                                         accent.withAlpha (0.0f), cx, cy, true);
                g.setGradientFill (fg);
                g.fillEllipse (cx - glowR, cy - glowR, glowR * 2.0f, glowR * 2.0f);

                g.setColour (accent.withAlpha (0.35f + 0.55f * heat));
                g.fillEllipse (cx - capR, cy - capR, capR * 2.0f, capR * 2.0f);

                if (heat > 0.02f)
                {
                    g.setColour (juce::Colour (0xffffe9c4).withAlpha (0.35f + 0.60f * heat));
                    g.fillEllipse (cx - capR * 0.55f, cy - capR * 0.55f,
                                   capR * 1.1f, capR * 1.1f);
                }
            }
            else
            {
                g.setColour (juce::Colour (0xff14181c));
                g.fillEllipse (cx - capR, cy - capR, capR * 2.0f, capR * 2.0f);
            }
        }

        }

        /* 10 · 轨道 + 11 · 值弧：仅矢量旋钮绘制。
           贴图旋钮（SSL/Manley/Pultec）自带指针与面板刻度，叠加彩色环显得多余
           （温润模式浅色面板上的绿色指示环已按需求去除） */
        if (tex < 0)
        {
        g.setColour (juce::Colour (tube   ? 0x596b5a45
                                   : silver ? 0x6695a0aa : 0x598c96a0));
        juce::Path track;
        track.addCentredArc (cx, cy, trackR, trackR, 0.0f, a0, a1, true);
        g.strokePath (track, juce::PathStrokeType (4.4f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::butt));

        /* 11 · 值弧（电子管 = 灯丝辉光：柔光晕亮度随热度，白热细芯居中） */
        const float from = bipolar ? (a0 + a1) * 0.5f : a0;
        if (std::abs (angle - from) > 0.004f)
        {
            juce::Path arc;
            arc.addCentredArc (cx, cy, trackR, trackR, 0.0f,
                               juce::jmin (from, angle), juce::jmax (from, angle), true);

            if (tube)
            {
                /* 灯丝：外柔光晕（随热度增强）→ 亮琥珀主体 → 白热细芯 */
                const std::pair<float, float> tp[] {
                    { 12.5f, 0.10f + 0.16f * heat },
                    {  8.2f, 0.22f + 0.24f * heat },
                    {  4.6f, 0.96f }
                };
                for (auto& [wPass, alpha] : tp)
                {
                    g.setColour (accent.withAlpha (alpha));
                    g.strokePath (arc, juce::PathStrokeType (wPass, juce::PathStrokeType::curved,
                                                             juce::PathStrokeType::rounded));
                }

                g.setColour (juce::Colour (0xffffe9c4).withAlpha (0.30f + 0.55f * heat));
                g.strokePath (arc, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
            }
            else
            {
                const std::pair<float, float> passes[] { { 9.6f, 0.16f }, { 6.4f, 0.28f }, { 4.4f, 1.0f } };
                for (auto& [wPass, alpha] : passes)
                {
                    g.setColour (accent.withAlpha (alpha));
                    g.strokePath (arc, juce::PathStrokeType (wPass, juce::PathStrokeType::curved,
                                                             juce::PathStrokeType::rounded));
                }
            }
        }
        }   // end if (tex < 0) — 轨道/值弧

        /* 12 · 指针（夜光白短刻度 + 偏移投影；贴图旋钮的指针是图内长槽，跳过） */
        if (tex < 0)
        {
        auto mkPtr = [&] (juce::Path& p, float dx, float dy, float dAng)
        {
            p.addRoundedRectangle (cx - 1.7f, cy - bodyR * 0.88f, 3.4f, bodyR * 0.33f, 1.6f);
            p.applyTransform (juce::AffineTransform::rotation (angle + dAng, cx, cy)
                                                       .translated (dx, dy));
        };

        juce::Path ptrS;
        mkPtr (ptrS, 1.1f, 1.5f, 0.03f);
        g.setColour (juce::Colour (0x59000000));
        g.fillPath (ptrS);

        juce::Path ptr;
        mkPtr (ptr, 0.0f, 0.0f, 0.0f);
        g.setColour (juce::Colour (tube ? 0xfff2e3c2
                                        : silver ? 0xff1d242b : 0xffeef4f9));
        g.fillPath (ptr);
        }
    }
};

/* ---------------------------------------------------------------------------
   带名称与数值的旋钮
--------------------------------------------------------------------------- */
class FusionKnob : public juce::Component,
                   private juce::AudioProcessorParameter::Listener,
                   private juce::AsyncUpdater
{
public:
    FusionKnob (juce::AudioProcessorValueTreeState& state,
                const juce::String& paramId,
                const juce::String& labelText,
                Sweep sweepCfg = {},
                bool bipolarKnob = false,
                std::function<juce::String (float)> formatter = nullptr)
        : param (*state.getParameter (paramId)),
          sweepRange (sweepCfg),
          isBipolar (bipolarKnob),
          formatFn (std::move (formatter))
    {
        jassert (state.getParameter (paramId) != nullptr);

        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange (0.0, 1.0, 0.0);                    // 归一化行程 = 参数归一化值
        slider.setRotaryParameters (
            juce::MathConstants<float>::pi * sweepCfg.startDeg / 180.0f,
            juce::MathConstants<float>::pi * sweepCfg.endDeg   / 180.0f,
            true);

        slider.getProperties().set ("bipolar", bipolarKnob ? 1 : 0);
        slider.setDoubleClickReturnValue (true, param.getDefaultValue());
        slider.setLookAndFeel (&lnf);
        slider.setWantsKeyboardFocus (true);

        nameLabel.setText (labelText, juce::dontSendNotification);
        nameLabel.setJustificationType (juce::Justification::centred);
        nameLabel.setFont (juce::Font (10.0f, juce::Font::bold));
        nameLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8b949c));

        valueLabel.setJustificationType (juce::Justification::centred);
        valueLabel.setFont (juce::Font (12.5f, juce::Font::bold));
        valueLabel.setColour (juce::Label::textColourId, juce::Colour (0xff1d242b));

        addAndMakeVisible (slider);
        addAndMakeVisible (nameLabel);
        addAndMakeVisible (valueLabel);

        slider.onDragStart   = [this] { param.beginChangeGesture(); };
        slider.onDragEnd     = [this] { param.endChangeGesture(); };
        slider.onValueChange = [this]
        {
            if (ignoreCallback)
                return;

            param.setValueNotifyingHost (sliderToParam ((float) slider.getValue()));
            updateDisplay();
        };

        param.addListener (this);

        slider.setValue (param.getValue(), juce::dontSendNotification);
        updateDisplay();
    }

    ~FusionKnob() override
    {
        param.removeListener (this);
        cancelPendingUpdate();
        slider.setLookAndFeel (nullptr);
    }

    void setAccentColour (juce::Colour c)
    {
        lnf.setColour (juce::Slider::rotarySliderFillColourId, c);
        repaint();
    }

    /* 银白机身（AIR 专属亮点） */
    void setSilverBody (bool shouldBeSilver)
    {
        slider.getProperties().set ("silver", shouldBeSilver ? 1 : 0);
        slider.repaint();
    }

    /* 隐藏名称/读数标签（步进钮等由外部刻度承担读数时），组件高度全部让给旋钮 */
    void setLabelsVisible (bool shouldBeVisible)
    {
        nameLabel.setVisible (shouldBeVisible);
        valueLabel.setVisible (shouldBeVisible);
        resized();
    }

    /* Manley 电子管皮肤：玻壳本体 + 琥珀灯丝弧（亮度随增益"点亮"）+ 发光灯芯 */
    void setTubeStyle (bool shouldBeTube)
    {
        slider.getProperties().set ("tube", shouldBeTube ? 1 : 0);
        slider.repaint();
    }

    /* SSL 4000E 贴图皮肤：0=黑 1=绿 2=蓝 3=紫，-1 = 关闭（回到矢量金属旋钮） */
    void setTextureStyle (int texIndex)
    {
        slider.getProperties().set ("tex", texIndex);
        slider.repaint();
    }

    /* 子行程：把旋钮 0~1 的行程映射到参数归一化区间 [lo01, hi01]。
       参数本身的全局范围不变，只是该模式下旋钮只扫这一段（如 SSL 各段频点）。
       越界的当前值会被钳进新区间（进 SSL 时旧工程的超界频点自动收进来）。 */
    void setSubRange (float lo01, float hi01)
    {
        subLo = juce::jlimit (0.0f, 1.0f, juce::jmin (lo01, hi01));
        subHi = juce::jlimit (0.0f, 1.0f, juce::jmax (lo01, hi01));

        if (subHi - subLo < 1.0e-4f)          // 防御：退化成零行程时回到全域
        { subLo = 0.0f; subHi = 1.0f; }

        const float pn      = param.getValue();
        const float clamped = juce::jlimit (subLo, subHi, pn);

        if (clamped != pn)
            param.setValueNotifyingHost (clamped);

        slider.setDoubleClickReturnValue (true,
            juce::jlimit (subLo, subHi, (float) param.getDefaultValue()));

        {
            const juce::ScopedValueSetter<bool> guard (ignoreCallback, true);
            slider.setValue (paramToSlider (clamped), juce::dontSendNotification);
            updateDisplay();
        }
    }

    /* 放大旋钮标题字（AIR 大字标） */
    void setLabelSize (float height, juce::Colour colour)
    {
        nameLabel.setFont (juce::Font (height, juce::Font::bold));
        nameLabel.setColour (juce::Label::textColourId, colour);
        nameLabel.repaint();
    }

    /* 步进行程：n 档（如 4 档频点选择），拖动/滚轮只落在档位上；<=1 = 连续 */
    void setStepCount (int n)
    {
        if (n <= 1)
            slider.setRange (0.0, 1.0, 0.0);
        else
            slider.setRange (0.0, 1.0, 1.0 / (double) (n - 1));
    }

    /* 离散频点档（Neve 1073 式频点选择）：给定 n 个 Hz 值，行程均分 n 档，
       每档把参数吸附到对应频率；空列表 = 恢复连续行程。
       与 setSubRange 不同——档位值精确落在给定 Hz，不受对数斜率影响。 */
    void setSnapHz (const juce::Array<float>& freqs)
    {
        snapNorm.clear();

        if (freqs.size() >= 2)
        {
            for (auto f : freqs)
                snapNorm.add ((double) param.convertTo0to1 (f));   // convertTo0to1 自带范围钳制
            slider.setRange (0.0, 1.0, 1.0 / (double) (snapNorm.size() - 1));
        }
        else
        {
            slider.setRange (0.0, 1.0, 0.0);
        }

        /* 当前值吸附到最近档并回写参数（保证音频与读数一致） */
        const float want   = sliderToParam (paramToSlider ((float) param.getValue()));
        const float curVal = (float) param.getValue();
        if (std::abs (want - curVal) > 1.0e-6f)
        {
            param.beginChangeGesture();
            param.setValueNotifyingHost (want);
            param.endChangeGesture();
        }
        {
            const juce::ScopedValueSetter<bool> guard (ignoreCallback, true);
            slider.setValue (paramToSlider (want), juce::dontSendNotification);
            updateDisplay();
        }
    }

    /* SSL 硬件皮肤：深色模块面板上的丝印白字 */
    void setDarkPanel (bool d)
    {
        if (darkPanel == d)
            return;

        darkPanel = d;
        nameLabel.setColour (juce::Label::textColourId,
            d ? juce::Colour (0xffc9d2d9) : juce::Colour (0xff8b949c));
        valueLabel.setColour (juce::Label::textColourId,
            d ? juce::Colour (0xfff3f7fa) : juce::Colour (0xff1d242b));
        nameLabel.repaint();
        valueLabel.repaint();
    }

    /* 读数移到旋钮上方（温暖 MP 的 GAIN：下方被 FREQUENCY 环绕刻度占用） */
    void setValueOnTop (bool b)
    {
        if (valueOnTop == b)
            return;
        valueOnTop = b;
        resized();
    }

    /* 仅隐藏名称丝印（温暖 MP 的 GAIN：量程拨杆已表意，名称冗余） */
    void setNameVisible (bool v)
    {
        if (nameLabel.isVisible() == v)
            return;
        nameLabel.setVisible (v);
        resized();
    }

    void resized() override
    {
        auto r = getLocalBounds();

        if (valueLabel.isVisible())
        {
            if (valueOnTop) valueLabel.setBounds (r.removeFromTop (16));
            else            valueLabel.setBounds (r.removeFromBottom (16));
        }
        if (nameLabel.isVisible())  nameLabel.setBounds  (r.removeFromBottom (13));
        slider.setBounds (r);
    }

    void paint (juce::Graphics&) override {}

    /* 锁定（AIR 关闭等）：阻止拖动 + 整体淡化（无矩形遮罩，边缘不割裂） */
    void setLocked (bool shouldBeLocked)
    {
        setEnabled (! shouldBeLocked);

        const float a = shouldBeLocked ? 0.38f : 1.0f;
        slider.setAlpha (a);
        nameLabel.setAlpha (a);
        valueLabel.setAlpha (a);
    }

private:
    bool darkPanel = false;

    /* 参数变化（宿主自动化 / 载入预置）→ 异步同步到界面 */
    void parameterValueChanged (int, float newNormalisedValue) override
    {
        pendingNormalised = newNormalisedValue;
        triggerAsyncUpdate();
    }

    void parameterGestureChanged (int, bool) override {}

    void handleAsyncUpdate() override
    {
        const juce::ScopedValueSetter<bool> guard (ignoreCallback, true);
        slider.setValue (paramToSlider (juce::jlimit (subLo, subHi, pendingNormalised)),
                         juce::dontSendNotification);
        updateDisplay();
    }

    /* 行程 ↔ 参数归一化值 的互转（subLo=0 / subHi=1 时即恒等）。
       snapNorm 非空时优先走离散档位映射（行程均分 ↔ 最近档位值）。 */
    float paramToSlider (float p) const noexcept
    {
        if (snapNorm.size() >= 2)
        {
            int   best = 0;
            double bd  = 1.0e9;
            for (int k = 0; k < snapNorm.size(); ++k)
            {
                const double d = std::abs (snapNorm.getUnchecked (k) - (double) p);
                if (d < bd) { bd = d; best = k; }
            }
            return (float) ((double) best / (double) (snapNorm.size() - 1));
        }

        const float span = juce::jmax (1.0e-6f, subHi - subLo);
        return juce::jlimit (0.0f, 1.0f, (p - subLo) / span);
    }

    float sliderToParam (float s) const noexcept
    {
        if (snapNorm.size() >= 2)
        {
            const int k = juce::jlimit (0, snapNorm.size() - 1,
                juce::roundToInt ((double) s * (double) (snapNorm.size() - 1)));
            return (float) snapNorm.getUnchecked (k);
        }

        return juce::jlimit (subLo, subHi, subLo + s * (subHi - subLo));
    }

    void updateDisplay()
    {
        const auto real = param.convertFrom0to1 (sliderToParam ((float) slider.getValue()));
        valueLabel.setText (formatFn ? formatFn (real) : juce::String (real, 2),
                            juce::dontSendNotification);
    }

    /* 注意：成员声明顺序 = 构造顺序 */
    KnobLookAndFeel lnf;
    juce::Slider slider;
    juce::Label nameLabel, valueLabel;
    bool valueOnTop = false;

    juce::RangedAudioParameter& param;

    Sweep sweepRange;
    bool isBipolar;
    std::function<juce::String (float)> formatFn;

    bool  ignoreCallback    = false;
    float pendingNormalised = 0.0f;
    float subLo = 0.0f, subHi = 1.0f;      // 子行程（参数归一化区间）
    juce::Array<double> snapNorm;           // 离散频点档（参数归一化值，升序）；空 = 连续

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FusionKnob)
};

} // namespace Fusion
