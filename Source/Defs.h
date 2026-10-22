/* =============================================================================
   Fusion EQ  ·  Defs.h
   模式定义表 —— 六种音色差异的唯一来源

   每个模式在四个维度上同时改变，缺一不可：
     1) Q 行为     qMul / proportionalQ
     2) 染色级     shaper / amount / makeup
     3) 静态倾斜   tiltLow / tiltMid / tiltHigh
     4) 低切手感   hpfQ / hpfHasOnePole  +  AIR 的 airStages / airQ

   ⚠ 编码约定（重要）：
   JUCE 的 String(const char*) 走 CharPointer_ASCII，会把 UTF-8 字节
   当成 Latin-1 逐字节解释 → 中文变乱码。所有非 ASCII 文本必须用 U8() 包装。
   ========================================================================== */
#pragma once
#include <JuceHeader.h>

namespace Fusion
{

/* 把 UTF-8 字节正确解码成 juce::String（所有中文文本都必须经过这里） */
inline juce::String U8 (const char* utf8) { return juce::String::fromUTF8 (utf8); }

/* ------------------------------ 常量 ------------------------------ */
static constexpr float fMin       = 20.0f;
static constexpr float fMax       = 22000.0f;
static constexpr float airFreq    = 11000.0f; // AIR 平行路径低切频点
static constexpr float hpfFreq    = 60.0f;    // 低切固定频点：曲线在 ≈100 Hz 归平
static constexpr int   numBands   = 6;
static constexpr float gainRange  = 20.0f;    // ±dB（Manley Massive Passive 需要 ±20）
static constexpr float qMin       = 0.1f;
static constexpr float qMax       = 40.0f;

/* 低切 / 高切段斜率档位（滚轮逐档，默认第 0 档 6 dB/oct）。
   全部用二阶级（12 dB/oct）叠加；奇数档额外补一个一阶（6 dB/oct）。
   末档 BRICKWALL = 192 dB/oct（16 节），即"砖墙"。 */
static constexpr int numCutSlopes = 10;
inline const std::array<float, numCutSlopes>& cutSlopeSteps()
{
    static const std::array<float, numCutSlopes> s { 6.0f, 12.0f, 18.0f, 24.0f, 30.0f,
                                                     36.0f, 48.0f, 72.0f, 96.0f, 192.0f };
    return s;
}
inline const std::array<juce::String, numCutSlopes>& cutSlopeNames()
{
    static const std::array<juce::String, numCutSlopes> s {
        "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "30 dB/oct",
        "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct", "BRICKWALL" };
    return s;
}
/* 当前斜率值 → 最近的档位下标 */
inline int cutSlopeIndex (float db) noexcept
{
    const auto& s = cutSlopeSteps();
    int best = 0;
    for (int i = 1; i < numCutSlopes; ++i)
        if (std::abs (db - s[(size_t) i]) < std::abs (db - s[(size_t) best]))
            best = i;
    return best;
}

/* 截止点补偿：已废弃。精确 N 阶 Butterworth 的 -3 dB 点本来就落在截止
   频率上（见 PluginProcessor.cpp cutSlopeQ），任何补偿都会把曲线推离节点。 */

/* AIR 平行增益：平方律，满程 +18 dB 平行叠加 */
static constexpr float airMaxLin  = 7.0f;
inline float airLinear (float a) noexcept { return a * a * airMaxLin; }
inline float airDb     (float a) noexcept
{
    return juce::Decibels::gainToDecibels (1.0f + airLinear (a));
}

/* ------------------------------ 显示格式 ------------------------------ */
inline juce::String formatFreq (float f)
{
    return f < 1000.0f ? juce::String (juce::roundToInt (f)) + " Hz"
                       : juce::String (f / 1000.0f, f < 10000.0f ? 2 : 1) + " kHz";
}
inline juce::String formatGain (float g)
{
    return (g >= 0.0f ? "+" : "") + juce::String (g, 1) + " dB";
}
inline juce::String formatQ (float q) { return juce::String (q, 2); }
inline juce::String formatAir (float a)
{
    /* AIR 是平行的音量混合：只显示混合比例，不显示频率与提升量 */
    if (a <= 0.0005f)
        return "OFF";
    return juce::String (juce::roundToInt (a * 100.0f)) + "%";
}

/* 段类型标签（与 BandType 枚举顺序一致） */
static const juce::StringArray& TYPE_LABELS()
{
    static const juce::StringArray a { "BELL", "LOW SHEL", "LOW CUT", "HIGH SHEL", "HIGH CUT",
                                       "NOTCH", "BAND PASS", "TILT SHEL", "ALL PASS" };
    return a;
}

/* 段显示名 */
inline const juce::StringArray& BAND_NAMES()
{
    static const juce::StringArray a { "LOW", "LOW-MID", "MID", "HI-MID", "PRESENCE", "HIGH" };
    return a;
}
inline const juce::StringArray& BAND_NAMES_CN()
{
    static const juce::StringArray a {
        U8 ("\u4f4e\u9891"), U8 ("\u4e2d\u4f4e\u9891"), U8 ("\u4e2d\u9891"),
        U8 ("\u4e2d\u9ad8\u9891"), U8 ("\u4e34\u573a\u5ea6"), U8 ("\u9ad8\u9891")
    };
    return a;
}

/* ------------------------------ 枚举 ------------------------------ */
enum class Mode { Clean = 0, Manley, Pultec, SSL, Neve, API, Count };
static constexpr int numModes = (int) Mode::Count;

/* 段类型：与 UI 菜单顺序一致 BELL → LOW SHEL → LOW CUT → HIGH SHEL → HIGH CUT
   → NOTCH → BAND PASS → TILT SHEL → ALL PASS */
enum class BandType { Bell = 0, LowShelf, LowCut, HighShelf, HighCut,
                      Notch, BandPass, TiltShelf, AllPass, Count };

/* EQ 类型菜单项的小曲线图标（类似经典调音台上的曲线示意）：
   BELL=中间凸起 / LOW SHEL=右端抬升 / HIGH SHEL=右端下压 /
   NOTCH=中间深谷 / BAND PASS=窄峰两侧全落底 */
inline std::unique_ptr<juce::Drawable> makeTypeIcon (int type, juce::Colour col)
{
    constexpr float yLo = 12.5f, yHi = 3.5f;

    juce::Path p;

    switch ((BandType) type)
    {
        case BandType::LowShelf:
            p.startNewSubPath (1.0f, yLo);
            p.lineTo (8.0f, yLo);
            p.cubicTo (14.0f, yLo, 14.0f, yHi, 20.0f, yHi);
            p.lineTo (25.0f, yHi);
            break;

        case BandType::HighShelf:
            p.startNewSubPath (1.0f, yHi);
            p.lineTo (7.0f, yHi);
            p.cubicTo (13.0f, yHi, 13.0f, yLo, 19.0f, yLo);
            p.lineTo (25.0f, yLo);
            break;

        case BandType::Notch:
            p.startNewSubPath (1.0f, yHi);
            p.lineTo (7.0f, yHi);
            p.cubicTo (10.5f, yHi, 10.5f, yLo, 13.0f, yLo);
            p.cubicTo (15.5f, yLo, 15.5f, yHi, 19.0f, yHi);
            p.lineTo (25.0f, yHi);
            break;

        case BandType::BandPass:
            p.startNewSubPath (1.0f, yLo);
            p.lineTo (6.5f, yLo);
            p.cubicTo (10.5f, yLo, 10.5f, yHi, 13.0f, yHi);
            p.cubicTo (15.5f, yHi, 15.5f, yLo, 19.5f, yLo);
            p.lineTo (25.0f, yLo);
            break;

        case BandType::LowCut:
            p.startNewSubPath (1.0f, yLo);
            p.lineTo (6.0f, yLo);
            p.cubicTo (11.0f, yLo, 11.0f, yHi, 16.0f, yHi);
            p.lineTo (25.0f, yHi);
            break;

        case BandType::HighCut:
            p.startNewSubPath (1.0f, yHi);
            p.lineTo (6.0f, yHi);
            p.cubicTo (11.0f, yHi, 11.0f, yLo, 16.0f, yLo);
            p.lineTo (25.0f, yLo);
            break;

        case BandType::TiltShelf:
            /* 平直倾斜：一条贯穿的斜线 */
            p.startNewSubPath (1.0f, yLo);
            p.lineTo (25.0f, yHi);
            break;

        case BandType::AllPass:
            /* 全通幅频是一条水平直线（只改变相位） */
            p.startNewSubPath (1.0f, (yLo + yHi) * 0.5f);
            p.lineTo (25.0f, (yLo + yHi) * 0.5f);
            break;

        case BandType::Bell:
        default:
            p.startNewSubPath (1.0f, yLo);
            p.cubicTo (8.0f, yLo, 9.5f, yHi, 13.0f, yHi);
            p.cubicTo (16.5f, yHi, 18.0f, yLo, 25.0f, yLo);
            break;
    }

    juce::Path outline;
    juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded)
        .createStrokedPath (outline, p);

    auto d = std::make_unique<juce::DrawablePath>();
    d->setPath (outline);
    d->setFill (col);
    return d;
}

/* 独立小耳机图标（频点监听菜单项用） */
inline std::unique_ptr<juce::Drawable> makeHeadphoneIcon (juce::Colour col)
{
    juce::Path p;
    /* 头带：上半圆弧 */
    p.addCentredArc (13.0f, 9.5f, 8.2f, 8.2f, 0.0f,
                     juce::MathConstants<float>::pi, juce::MathConstants<float>::twoPi, true);
    /* 左耳罩 */
    p.addRoundedRectangle (4.0f, 9.0f, 3.6f, 6.5f, 1.6f);
    /* 右耳罩 */
    p.addRoundedRectangle (18.4f, 9.0f, 3.6f, 6.5f, 1.6f);

    juce::Path outline;
    juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded)
        .createStrokedPath (outline, p);

    auto d = std::make_unique<juce::DrawablePath>();
    d->setPath (outline);
    d->setFill (col);
    return d;
}

enum class Shaper { Linear = 0, Tube, TubeLite, Vca, Transformer, OpAmp, Count };

/* ------------------------------ 模式规格 ------------------------------ */
struct BandDef
{
    BandType type;
    float    freq;
    float    gainDb;
    float    q;
};

struct TiltDef
{
    float freq;
    float gainDb;
    float q;
};

struct ModeSpec
{
    const char* id;
    juce::String en;        // 英文短名
    juce::String cn;        // 中文短名
    juce::String tag;       // 右侧小标签
    juce::String desc;      // 模式说明
    juce::uint32 accent;
    juce::uint32 accentDark;

    /* 启用的频段位掩码（bit i = Band i）。
       SSL 模式模仿 SSL 4000E 通道 EQ 的 4 段结构（LF / LMF / HMF / HF），
       对应内部 Band 1/3/4/6（0 基 0/2/3/5），屏蔽 LOW-MID 与 PRESENCE。 */
    juce::uint32 bandMask;

    /* 低切 */
    float hpfQ;
    bool  hpfHasOnePole;   // true = 二级联(2阶+1阶) → 18 dB/oct

    /* Q 行为 */
    float qMul;
    bool  proportionalQ;

    /* 染色（shaper 已归一化为"小信号斜率 1"，故 makeup 即整体增益） */
    Shaper shaper;
    float  amount;
    float  drive;
    float  makeup;

    /* 静态倾斜 */
    TiltDef tiltLow, tiltMid, tiltHigh;

    /* AIR 平行路径 */
    int   airStages;
    float airQ;

    /* 该模式的标志性起点 */
    BandDef bands[numBands];
};

/* 用宏把重复的默认值压缩掉，避免 6×6 行重复 */
#define FUSION_BAND(T, F, G, Q) { BandType::T, F, G, Q }
#define FUSION_TILT(F, G, Q)    { F, G, Q }

inline const std::array<ModeSpec, numModes>& getModeSpecs()
{
    static const std::array<ModeSpec, numModes> specs =
    {{
        /* ---------------- CLEAN ---------------- */
        {
            "clean", "CLEAN", U8 ("\u5e72\u51c0"),
            "EQ-6",
            U8 ("\u5b8c\u5168\u7ebf\u6027\u7684\u4fe1\u53f7\u8def\u5f84\uff0c\u4e0d\u52a0\u4efb\u4f55\u8c10\u6ce2\u3001\u9971\u548c\u4e0e\u76f8\u4f4d\u67d3\u8272\uff1b\u6052\u5b9a Q\u3001\u7cbe\u786e\u5bf9\u79f0\u7684\u6700\u5c0f\u76f8\u4f4d\u6ee4\u6ce2\u3002\u9002\u5408\u6bcd\u5e26\u7ea7\u7cbe\u4fee\u4e0e\u9700\u8981\u7edd\u5bf9\u5ba2\u89c2\u7684\u6280\u672f\u6027\u4fee\u6b63\u3002"),
            0xff0e9bd6, 0xff0a7cae,
            0x3f,
            0.707f, false,
            1.00f, false,
            Shaper::Linear, 0.00f, 1.0f, 1.000f,
            FUSION_TILT(120.0f,  0.0f, 0.70f),
            FUSION_TILT(1000.0f, 0.0f, 0.80f),
            FUSION_TILT(9000.0f, 0.0f, 0.70f),
            4, 0.707f,
            {
                FUSION_BAND(LowCut,60.0f,   0.0f, 0.70f),
                FUSION_BAND(Bell,      200.0f,  0.0f, 1.00f),
                FUSION_BAND(Bell,      600.0f,  0.0f, 1.00f),
                FUSION_BAND(Bell,      2500.0f, 0.0f, 1.00f),
                FUSION_BAND(Bell,      6000.0f, 0.0f, 1.00f),
                FUSION_BAND(HighShelf, 12000.0f,0.0f, 0.70f)
            }
        },
        /* ---------------- MANLEY ---------------- */
        {
            "manley", "MANLEY", U8 ("\u6e29\u6696"),
            "Tube EQ",
            U8 ("\u4eff\u7535\u5b50\u7ba1\u63a8\u633d\u653e\u5927\u7ea7\u7684\u67d4\u548c\u5076\u6b21\u8c10\u6ce2\uff1a\u4f4e\u9891\u80a5\u539a\u6709\u8eab\u4f53\u611f\u3001\u9ad8\u9891\u4e1d\u7ef8\u822c\u987a\u6ed1\uff0c\u62d0\u70b9\u6781\u8f6f\u5e76\u5e26\u6709\u81ea\u7136\u7684\u547c\u5438\u4e0e\u8f7b\u5fae\u538b\u7f29\u3002\u5207\u4f4e\u9891\u65f6\u4e5f\u66f4\u677e\u8f6f\u3001\u4e0d\u7d27\u7ef7\u3002"),
            0xffe08a2b, 0xffb96a15,
            0x2d,   /* Manley Massive Passive 4 段：Band 0/2/3/5（同 SSL/API 段位） */
            0.55f, false,
            0.75f, false,
            Shaper::Tube, 0.55f, 1.0f, 0.975f,
            FUSION_TILT(110.0f,  0.4f, 0.70f),
            FUSION_TILT(420.0f, -0.2f, 0.70f),
            FUSION_TILT(9000.0f, 0.5f, 0.70f),
            3, 0.620f,
            {
                /* Massive Passive：被动网络默认全平（FLAT = 增益钮最逆时针），
                   各段 BELL 形态、频点落在各段量程中点附近 */
                FUSION_BAND(Bell, 100.0f,  0.0f, 0.70f),
                FUSION_BAND(Bell, 220.0f,  0.0f, 0.70f),
                FUSION_BAND(Bell, 560.0f,  0.0f, 0.70f),
                FUSION_BAND(Bell, 1500.0f, 0.0f, 0.70f),
                FUSION_BAND(Bell, 4700.0f, 0.0f, 0.70f),
                FUSION_BAND(Bell, 8200.0f, 0.0f, 0.70f)
            }
        },
        /* ---------------- PULTEC ---------------- */
        {
            "pultec", "PULTEC", U8 ("\u6e29\u6da6"),
            U8 ("\u88ab\u52a8 \u00b7 \u590d\u53e4"),
            U8 ("\u65e0\u6e90 LC \u7f51\u7edc\uff1a\u66f2\u7ebf\u6781\u5bbd\u3001\u6781\u7f13\u3001\u5f7c\u6b64\u8fde\u8d2f\uff0c\u6ca1\u6709\u6570\u5b57\u5473\u7684\u9510\u5229\u611f\u3002\u7ecf\u5178\u4f4e\u9891\u9b54\u672f\u2014\u2014\u4f4e\u9891\u6bb5\u53ef\u540c\u65f6\u63d0\u5347\u4e0e\u8870\u51cf\uff0c\u5f97\u5230\u53c8\u539a\u53c8\u5e72\u51c0\u7684\u5e95\u90e8\u3002"),
            0xff68753f, 0xff4e592c,
            0x00,   /* Pultec 三段式：6 个常规频段全部屏蔽，改用 plLow/plBand/plHigh 参数 */
            0.50f, false,
            0.55f, false,
            Shaper::TubeLite, 0.30f, 1.0f, 0.983f,
            FUSION_TILT(100.0f,  0.3f, 0.70f),
            FUSION_TILT(3000.0f, 0.2f, 0.60f),
            FUSION_TILT(8000.0f, 0.2f, 0.70f),
            2, 0.550f,
            {
                FUSION_BAND(LowShelf,  30.0f,   2.0f, 0.60f),
                FUSION_BAND(Bell,      250.0f, -1.5f, 0.90f),
                FUSION_BAND(Bell,      700.0f,  0.0f, 0.60f),
                FUSION_BAND(Bell,      3000.0f, 0.6f, 0.55f),
                FUSION_BAND(Bell,      5000.0f, -0.6f, 0.60f),
                FUSION_BAND(HighShelf, 10000.0f,1.5f, 0.60f)
            }
        },
        /* ---------------- SSL ---------------- */
        {
            "ssl", "SSL", U8 ("\u901a\u900f"),
            U8 ("\u6052\u5b9a Q \u00b7 \u7cbe\u51c6"),
            U8 ("\u6052\u5b9a Q\uff1a\u5e72\u51c0\u3001\u5feb\u901f\u3001\u6781\u5177\u7a7f\u900f\u529b\u7684\u82f1\u4f26\u8c03\u97f3\u53f0\u8d28\u611f\u3002\u6ee4\u6ce2\u5668\u8fb9\u7f18\u9510\u5229\uff0c\u9002\u5408\u7cbe\u51c6\u7684\u95ee\u9898\u9891\u6bb5\u5904\u7406\uff1b\u4f4e\u5207 18 dB/oct\uff0c\u6536\u5f97\u5e72\u51c0\u5229\u843d\u3001\u6beb\u4e0d\u62d6\u6ce5\u5e26\u6c34\u3002"),
            0xff8464c8, 0xff644aa0,
            0x2d,
            0.707f, true,
            1.35f, false,
            Shaper::Vca, 0.30f, 1.0f, 0.987f,
            FUSION_TILT(120.0f,  0.0f, 0.70f),
            FUSION_TILT(2500.0f, 0.1f, 0.90f),
            FUSION_TILT(11000.0f,0.2f, 0.70f),
            5, 0.707f,
            {
                FUSION_BAND(LowShelf,  60.0f,   0.0f, 0.70f),
                FUSION_BAND(Bell,      220.0f,  0.0f, 1.40f),
                FUSION_BAND(Bell,      700.0f,  0.0f, 2.00f),
                FUSION_BAND(Bell,      2500.0f, 0.0f, 2.00f),
                FUSION_BAND(Bell,      6000.0f, 0.0f, 1.40f),
                FUSION_BAND(HighShelf, 12000.0f,0.0f, 0.70f)
            }
        },
        /* ---------------- NEVE ---------------- */
        {
            "neve", "NEVE", U8 ("\u539a\u5b9e"),
            "N-EQ",
            U8 ("\u7535\u611f\u5f0f\u5747\u8861 + \u53d8\u538b\u5668\u67d3\u8272\uff1a\u5bbd Q \u5927\u5200\u9614\u65a7\uff0c\u4e2d\u9891\u539a\u5b9e\u6709\u80f8\u8154\u611f\uff0c\u4e8c\u4e09\u6b21\u8c10\u6ce2\u5bc6\u5ea6\u660e\u663e\u3002\u4f4e\u5207\u5e26\u8f7b\u5fae\u8c10\u632f\u51f8\u8d77\uff0c\u6536\u4f4e\u9891\u7684\u540c\u65f6\u53cd\u800c\u8ba9\u5e95\u9f13\u66f4\u9876\u3002"),
            0xff6d7a82, 0xff515c63,
            0x25,   /* NEVE 1073 三段式：LOW(0) / MID(2) / HIGH(5)，屏蔽 LOW-MID 与 PRESENCE */
            1.05f, true,
            0.70f, false,
            Shaper::Transformer, 0.60f, 1.0f, 0.992f,
            FUSION_TILT(110.0f,  0.3f, 0.70f),
            FUSION_TILT(1600.0f, 0.3f, 0.55f),
            FUSION_TILT(10000.0f,0.2f, 0.70f),
            4, 0.750f,
            {
                /* 1073 默认全平：打开插件不做任何处理（同 Manley 规则） */
                FUSION_BAND(LowShelf,  60.0f,   0.0f, 0.50f),
                FUSION_BAND(Bell,      220.0f,  0.0f, 0.60f),
                FUSION_BAND(Bell,      900.0f,  0.0f, 0.60f),
                FUSION_BAND(Bell,      3200.0f, 0.0f, 0.70f),
                FUSION_BAND(Bell,      6000.0f, 0.0f, 0.80f),
                FUSION_BAND(HighShelf, 12000.0f,0.0f, 0.60f)
            }
        },
        /* ---------------- API ---------------- */
        {
            "api", "API", U8 ("\u786c\u6717"),
            "A-EQ",
            U8 ("\u6bd4\u4f8b Q\u2014\u2014\u589e\u76ca\u8d8a\u5927\u5e26\u5bbd\u8d8a\u7a84\u3001\u5c0f\u589e\u76ca\u65f6\u6781\u5bbd\uff0c\u662f\u505a\u97f3\u8272\u5851\u5f62\u6700\u5feb\u7684\u624b\u611f\uff1b\u79bb\u6563\u8fd0\u653e\u7684\u786c\u6717\u51b2\u51fb\u529b\u4e0e\u7d27\u5b9e\u4f4e\u9891\u3002\u4f4e\u5207 12 dB/oct \u5e26\u660e\u663e\u8c10\u632f\uff0c\u6536\u4f4e\u9891\u53cd\u800c\u66f4\u6253\u3002"),
            0xff2450d0, 0xff183a9e,
            0x2d,        // API 550L 4 段：Band 0/2/3/5 = LF/LMF/HMF/HF（同 SSL 段位）
            1.15f, false,
            1.00f, true,
            Shaper::OpAmp, 0.40f, 1.0f, 0.986f,
            FUSION_TILT(100.0f,  0.2f, 0.70f),
            FUSION_TILT(3500.0f, 0.3f, 0.70f),
            FUSION_TILT(8000.0f, 0.3f, 0.70f),
            4, 0.850f,
            {
                /* 550L 默认全平：打开插件不做任何处理 */
                FUSION_BAND(LowShelf,  100.0f,  0.0f, 0.80f),
                FUSION_BAND(Bell,      250.0f,  0.0f, 1.00f),
                FUSION_BAND(Bell,      1000.0f, 0.0f, 1.20f),
                FUSION_BAND(Bell,      3000.0f, 0.0f, 1.20f),
                FUSION_BAND(Bell,      5000.0f, 0.0f, 1.00f),
                FUSION_BAND(HighShelf, 12000.0f,0.0f, 0.70f)
            }
        }
    }};
    return specs;
}

inline const ModeSpec& getSpec (Mode m) noexcept
{
    return getModeSpecs()[(int) m];
}

/* 频段在该模式下是否启用（SSL 只启用 4 段） */
inline bool isBandActive (Mode m, int i) noexcept
{
    return (getSpec (m).bandMask >> (juce::uint32) i) & 1u;
}

/* 低切斜率说明（显示在 HPF 条上） */
inline juce::String getHpfNote (Mode m)
{
    switch (m)
    {
        case Mode::Clean:  return U8 ("12 dB/oct \u00b7 \u5df4\u7279\u6c83\u65af\uff0c\u4e2d\u6027\u65e0\u8c10\u632f");
        case Mode::Manley: return U8 ("12 dB/oct \u00b7 \u7f13\u80a9\uff0c\u7535\u5b50\u7ba1\u5f0f\u677e\u8f6f\u6536\u675f");
        case Mode::Pultec: return U8 ("12 dB/oct \u00b7 \u65e0\u6e90\u7f13\u5761\uff0c\u4fdd\u7559\u5e95\u90e8\u91cd\u91cf");
        case Mode::SSL:    return U8 ("18 dB/oct \u00b7 \u5e72\u51c0\u5229\u843d\uff0c\u7cbe\u51c6\u53bb\u9664\u6d51\u6d4a");
        case Mode::Neve:   return U8 ("18 dB/oct \u00b7 \u5e26\u8f7b\u5fae\u8c10\u632f\uff0c\u6536\u4f4e\u9891\u66f4\u9876");
        case Mode::API:    return U8 ("12 dB/oct \u00b7 \u5f3a\u8c10\u632f\uff0c\u6536\u4f4e\u9891\u66f4\u5177\u51b2\u51fb");
        default:           return {};
    }
}

} // namespace Fusion
