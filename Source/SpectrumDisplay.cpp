/* =============================================================================
   Fusion EQ  ·  SpectrumDisplay.cpp
   ========================================================================== */
#include "SpectrumDisplay.h"
#include "PluginProcessor.h"
#include "ParamIDs.h"

#include <cstring>

namespace
{
    constexpr float fMin = Fusion::fMin;
    constexpr float fMax = Fusion::fMax;
    const float logRatio = std::log (fMax / fMin);

    /* 频率网格：细密的一组分频线（Hz）—— 上限跟全局 fMax（22k）对齐 */
    const float gridFreqs[] = { 20.0f, 30.0f, 50.0f, 70.0f, 100.0f, 200.0f, 300.0f,
                                500.0f, 700.0f, 1000.0f, 2000.0f, 3000.0f, 5000.0f,
                                7000.0f, 10000.0f, 15000.0f, 20000.0f, 22000.0f };

    /* 需要标文字的频点 */
    const float labelFreqs[] = { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f,
                                 2000.0f, 5000.0f, 10000.0f, 22000.0f };

    /* 频点范围钳制：非 SSL 上限保持 20k（22k 仅 SSL HF）；SSL 按各段原版范围。
       bandMask 0x2d → LF=0 (40-600) / LMF=2 (200-2k) / HMF=3 (600-7k) / HF=5 (1.5k-22k) */
    float sslClampFreq (const FusionEQAudioProcessor& proc, int band, float f)
    {
        if (proc.getMode() != Fusion::Mode::SSL)
            return juce::jlimit (Fusion::fMin, 20000.0f, f);

        switch (band)
        {
            case 0:  return juce::jlimit (40.0f,   600.0f,  f);
            case 2:  return juce::jlimit (200.0f,  2000.0f, f);
            case 3:  return juce::jlimit (600.0f,  7000.0f, f);
            case 5:  return juce::jlimit (1500.0f, 22000.0f, f);
            default: return f;
        }
    }
}

/* ============================ 构造 ============================ */
SpectrumDisplay::SpectrumDisplay (FusionEQAudioProcessor& p) : processor (p)
{
    peaks.assign ((size_t) FusionEQAudioProcessor::spectrumBins, -100.0f);
    /* 默认保留频点交互；SSL 通透模式由编辑器调用 setNodesVisible(false)
       切成纯观察视图（不画频点、不响应鼠标）。 */
    setInterceptsMouseClicks (true, false);
    setOpaque (true);
    startTimerHz (60);

    processor.readSpectrum (specLocal);
    processor.readCurve (curveLocal);
}

SpectrumDisplay::~SpectrumDisplay() { stopTimer(); }

void SpectrumDisplay::setAccentColour (juce::Colour c)
{
    accent = c;
    staticDirty = true;
    repaint();
}

void SpectrumDisplay::resized()
{
    plotX = padL;
    plotY = padT;
    plotW = juce::jmax (1, getWidth()  - padL - padR);
    plotH = juce::jmax (1, getHeight() - padT - padB);

    staticDirty = true;
}

/* 参数快照的廉价哈希：只要它没变，曲线/网格就不需要重画 */
juce::uint64 SpectrumDisplay::computeStateHash() const
{
    juce::uint64 h = 1469598103934665603ull;

    const auto mix = [&h] (juce::int64 v)
    {
        h ^= (juce::uint64) v;
        h *= 1099511628211ull;
    };

    const auto q = [] (float v, float s)
    {
        return (juce::int64) std::llround ((double) v * (double) s);
    };

    const auto raw = [this] (const juce::String& id)
    {
        const auto* p = processor.apvts.getRawParameterValue (id);
        return p != nullptr ? p->load() : 0.0f;
    };

    mix ((juce::int64) processor.getMode());
    mix (q (raw (IDs::hpfOn),  10.0f));

    for (int i = 0; i < Fusion::numBands; ++i)
    {
        mix (q (raw (IDs::bOn   (i)),       10.0f));
        mix (q (raw (IDs::bType (i)),       10.0f));
        mix (q (raw (IDs::bFreq (i)),      100.0f));
        mix (q (raw (IDs::bGain (i)),      100.0f));
        mix (q (raw (IDs::bQ    (i)),      100.0f));
        mix (q (raw (IDs::bSlope(i)),      100.0f));   // 低切/高切斜率 → 曲线要重画
        mix (q (raw (IDs::bChan (i)),       10.0f));   // 声道模式影响段颜色 → 触发曲线缓存重绘
    }

    mix ((juce::int64) (highlightBand + 8));
    mix ((juce::int64) (activeBand + 8));

    return h;
}

void SpectrumDisplay::timerCallback()
{
    /* BYPASS 状态感知：面板 BYPASS 按钮 + 宿主的 bypass 参数任一置位都算。
       切状态时由 setBypassed 负责切灰 / 恢复交互并清理进行中的手势。 */
    {
        bool b = processor.isBypassed();

        if (auto* bp = processor.getBypassParameter())
            b = b || bp->getValue() > 0.5f;

        setBypassed (b);
    }

    /* 每帧先把快照拷贝到本地（seqlock 防撕裂），绘制期间音频线程
       随时可能发布新快照，绝不能直接引用共享缓冲。
       读取失败只说明这一瞬快照正被写入 —— 沿用上一帧本地拷贝继续画，
       绝不能把 snapshotsValid 翻回 false，否则泪滴/黄虚线会整帧消失闪烁 */
    if (processor.readSpectrum (specLocal)
      & processor.readCurve   (curveLocal))
        snapshotsValid = true;

    /* 曲线只在参数变化时重建。
       快照由 UI 侧主动刷新：updateResponseSnapshot 现在完全自包含
       （直接从参数现算系数），宿主停止播放（音频线程不跑）时低切
       开关等也能立刻反映到曲线上。 */
    if (computeStateHash() != lastStateHash)
    {
        processor.refreshCurveFromUI();
        processor.readCurve (curveLocal);        // 刷新后重读
        staticDirty = true;
        lastSnapVersion = processor.snapVersion.load();
        repaint();
    }
    else if (processor.snapVersion.load() != lastSnapVersion)
    {
        /* 音频线程在背后重算过快照（如播放中切换模式）——
           旧逻辑感知不到这种情况，曲线会一直停留在旧状态 */
        processor.readCurve (curveLocal);
        staticDirty = true;
        lastSnapVersion = processor.snapVersion.load();
        repaint();
    }

    /* 动态压缩进行中（干净模式、有段开 DYN 且 GR < -0.05 dB）：
       实时动态曲线随 GR 下沉，需要每帧重画 */
    if (processor.getMode() == Fusion::Mode::Clean)
        for (int i = 0; i < Fusion::numBands; ++i)
            if (processor.getBandDynOn (i) && processor.getBandDynGr (i) < -0.05f)
            {
                repaint();
                break;
            }

    /* 压缩旋钮区淡入淡出：跟随当前悬浮频点的 DYN 开关 */
    {
        const int hb = hudBand();
        const float target = (processor.getMode() == Fusion::Mode::Clean
                              && hb >= 0 && processor.getBandDynOn (hb)) ? 1.0f : 0.0f;

        if (std::abs (hudDynVis - target) > 0.001f)
        {
            hudDynVis += (target - hudDynVis) * 0.1f;   // ≈ 100 ms 淡入/淡出

            if (std::abs (hudDynVis - target) < 0.01f)
                hudDynVis = target;

            repaint();
        }
    }

    /* 峰值保持（按时间衰减，与帧率无关） */
    const auto& spec = specLocal;
    const auto  dt   = 1.0f / 60.0f;
    const auto  fall = (holdPeaks ? 10.0f : 66.0f) * dt;   // dB / 帧

    for (size_t i = 0; i < peaks.size() && i < spec.size(); ++i)
    {
        if (spec[i] > peaks[i])
            peaks[i] = spec[i];
        else
            peaks[i] = juce::jmax (spec[i], peaks[i] - fall);
    }

    /* 悬浮框淡出：悬停/拖动/监听期间常显；
       鼠标离开后延迟 0.5 秒再开始淡出（离开瞬间不闪没） */
    {
        const int hb = hudBand();

        if (hb >= 0)
            hudShowBand = hb;                    // 淡出期间继续画这一段

        /* 鼠标正压在浮窗上（含 10px 缓冲）时**不淡出**并把不透明度补满。
           原先只按 hudBand() 判定：鼠标一旦离开频点、浮窗立刻进入 0.5s
           宽限并开始淡出（约 0.625s 后 alpha < 0.5），而从频点移到
           右上角 × 通常就要 0.6s 左右 —— 结果就是"× 永远点不到"。 */
        const auto  mp    = getMouseXYRelative().toFloat();
        const bool  onHud = hudShowBand >= 0
                            && hudRect (hudShowBand).expanded (10.0f).contains (mp);

        if (hb < 0 && ! onHud)
        {
            hudIdleMs += (int) (dt * 1000.0f);

            if (hudIdleMs >= 500)
            {
                hudAlpha = juce::jmax (0.0f, hudAlpha - dt * 4.0f);

                if (hudAlpha <= 0.0f)
                {
                    hudShowBand  = -1;           // 淡出完成才收掉
                    hudBelowBand = -1;           // 清放置滞回，下次弹出重新决定上/下
                }
            }
        }
        else
        {
            hudIdleMs = 0;
            hudAlpha  = juce::jmin (1.0f, hudAlpha + dt * 6.0f);
        }
    }

    /* 只重画绘图区，标签与网格都走缓存 */
    repaint (juce::Rectangle<int> (plotX, plotY, plotW, plotH).expanded (12));
}

/* ============================ 坐标 ============================ */
float SpectrumDisplay::xOfFreq (float f) const noexcept
{
    const auto t = std::log (juce::jlimit (fMin, fMax, f) / fMin) / logRatio;
    return (float) plotX + t * (float) plotW;
}

float SpectrumDisplay::freqOfX (float x) const noexcept
{
    const auto t = juce::jlimit (0.0f, 1.0f, (x - (float) plotX) / (float) plotW);
    return fMin * std::exp (t * logRatio);
}

float SpectrumDisplay::yOfEq (float db) const noexcept
{
    const auto t = (eqTop - juce::jlimit (eqBot, eqTop, db)) / (eqTop - eqBot);
    return (float) plotY + t * (float) plotH;
}

float SpectrumDisplay::gainOfY (float y) const noexcept
{
    const auto t = juce::jlimit (0.0f, 1.0f, (y - (float) plotY) / (float) plotH);
    return eqTop - t * (eqTop - eqBot);
}

float SpectrumDisplay::yOfSpec (float db) const noexcept
{
    const auto t = (specTop - juce::jlimit (specBot, specTop, db)) / (specTop - specBot);
    return (float) plotY + t * (float) plotH;
}

juce::Colour SpectrumDisplay::bandColour (int i) const
{
    /* SSL 模式：颜色对齐下方四色瓷面旋钮（LF黑 / LMF绿 / HMF蓝 / HF紫） */
    if (sslPalette)
    {
        static const juce::Colour sslCols[6] = {
            juce::Colour (0xff1d1d1d), juce::Colour (0xff2f7fd8), juce::Colour (0xff77bf5b),
            juce::Colour (0xff45b8e4), juce::Colour (0xff2f9e6a), juce::Colour (0xff946cbb)
        };
        return sslCols[(size_t) juce::jlimit (0, 5, i)];
    }

    static const juce::Colour cols[6] = {
        juce::Colour (0xff2f7fd8), juce::Colour (0xffe08a2b), juce::Colour (0xffc0392b),
        juce::Colour (0xff2f9e6a), juce::Colour (0xff8e5bd0), juce::Colour (0xff0e9bd6)
    };

    /* 干净模式：声道模式覆盖段颜色（Pro-Q 式）——
       Left=红 / Right=橙 / Mid=绿 / Side=蓝，Stereo 保持原配色 */
    const int cm = processor.getBandChan (i);

    static const juce::Colour chanCols[5] = {
        juce::Colour (0x00000000),             // Stereo：不用
        juce::Colour (0xffe5484d),             // Left：红
        juce::Colour (0xffe8863a),             // Right：橙
        juce::Colour (0xff46c46a),             // Mid：绿
        juce::Colour (0xffa879ff)              // Side：紫
    };

    if (cm != 0)
        return chanCols[cm];

    return cols[(size_t) juce::jlimit (0, 5, i)];
}

/* ============================ 绘制 ============================ */
void SpectrumDisplay::rebuildStaticLayer()
{
    const auto w = juce::jmax (1, getWidth());
    const auto h = juce::jmax (1, getHeight());

    const auto ensure = [w, h] (juce::Image& img)
    {
        if (! img.isValid() || img.getWidth() != w || img.getHeight() != h)
            img = juce::Image (juce::Image::ARGB, w, h, true);
    };

    ensure (bgLayer);
    ensure (curveLayer);

    auto area = juce::Rectangle<int> (0, 0, w, h);
    auto plot = juce::Rectangle<int> (plotX, plotY, plotW, plotH);

    /* --- 背景层：面板底 + 绘图区 + 网格 ---
       深色皮肤 = 各模式机架面板色（SSL 灰蓝 / Pultec 石墨 / Neve 蓝灰 /
       API·Manley 黑），绘图区做成内嵌屏幕的凹陷质感 */
    {
        juce::Graphics g (bgLayer);

        const bool dark = skin != 0;

        /* 面板底：与机架同色系的渐变（顶部略亮的金属高光） */
        static const juce::uint32 bgT[6] = { 0xfff8fafc, 0xff5a6167, 0xff4e5358,
                                             0xff606d76, 0xff3c3e41, 0xff2c2e31 };
        static const juce::uint32 bgB[6] = { 0xffe9eef4, 0xff3b4046, 0xff2a2e33,
                                             0xff414c53, 0xff131416, 0xff0e0f11 };
        juce::ColourGradient bg (juce::Colour (bgT[(size_t) juce::jlimit (0, 5, skin)]), 0.0f, 0.0f,
                                 juce::Colour (bgB[(size_t) juce::jlimit (0, 5, skin)]), 0.0f,
                                 (float) area.getBottom(), false);
        g.setGradientFill (bg);
        g.fillRoundedRectangle (area.toFloat(), 6.0f);

        /* 绘图区：比面板更沉的内嵌屏 */
        static const juce::uint32 plotFill[6] = { 0xffffffff, 0xff454b52, 0xff363b41,
                                                  0xff4d585f, 0xff1d1f22, 0xff191a1d };
        g.setColour (juce::Colour (plotFill[(size_t) juce::jlimit (0, 5, skin)]));
        g.fillRect (plot);

        /* 边框：深色皮肤用深描边 + 上缘一线内侧高光，做出「沉进去」的感觉 */
        if (dark)
        {
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.drawRect (plot, 1);
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.drawHorizontalLine (plot.getY() + 1, (float) plot.getX() + 1,
                                  (float) plot.getRight() - 1);
        }
        else
        {
            g.setColour (juce::Colour (0xffd8dee6));
            g.drawRect (plot, 1);
        }

        drawGrid (g, plot);
    }

    /* --- 曲线层：透明底，单独一张，方便画在频谱之上 ---
           必须把内存真正归零再画：Graphics::fillAll(透明色) 是空操作
           （JUCE 内部对 isTransparent() 直接返回），Image 复用时旧内容
           会残留叠加成“重影”。 */
    {
        juce::Image::BitmapData bd (curveLayer, juce::Image::BitmapData::writeOnly);
        std::memset (bd.data, 0, (size_t) bd.lineStride * (size_t) bd.height);

        juce::Graphics g (curveLayer);
        /* 底部内缩 6px：低切这类大幅衰减会把曲线钳位在 -18 dB 的画布底边，
           沿底边拖出的那条水平线很难看 —— 连同它的描边阴影一起裁掉 */
        g.reduceClipRegion (plot.expanded (3).withTrimmedBottom (6));
        drawCurves (g, plot);
    }

    lastStateHash = computeStateHash();
    staticDirty = false;
}

void SpectrumDisplay::paint (juce::Graphics& g)
{
    if (staticDirty || ! bgLayer.isValid())
        rebuildStaticLayer();

    auto plot = juce::Rectangle<int> (plotX, plotY, plotW, plotH);

    if (bgLayer.isValid())
        g.drawImageAt (bgLayer, 0, 0, false);

    drawSpectrum (g, plot);

    if (curveLayer.isValid())
        g.drawImageAt (curveLayer, 0, 0, false);

    drawDynCurves (g, plot);

    if (showNodes && ! bypassVisual)
        drawNodes (g, plot);

    /* BYPASS：整块频谱压灰 + 居中提示。
       画在最上层，把曲线/频谱/频点/浮窗一并盖住，直观表达"看得到但动不了"。 */
    if (bypassVisual)
    {
        auto area = getLocalBounds();

        g.setColour (juce::Colour (0x8c9aa3a6));        // 半透明中性灰
        g.fillRect (area);

        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.setFont (juce::Font (juce::Font::getDefaultSansSerifFontName(), 15.0f, juce::Font::bold));
        g.drawText ("BYPASSED", area, juce::Justification::centred);
    }
}

void SpectrumDisplay::drawGrid (juce::Graphics& g, juce::Rectangle<int> plot)
{
    /* 深色皮肤：网格/刻度转浅色丝印（与机架硬件丝印同一观感） */
    const bool dark = skin != 0;

    /* --- 纵向频率线 --- */
    for (auto f : gridFreqs)
    {
        const auto x = xOfFreq (f);
        const bool major = (f == 20.0f || f == 100.0f || f == 1000.0f || f == 10000.0f);

        g.setColour (dark ? juce::Colour (major ? 0x46dfe6ee : 0x1fdfe6ee)
                          : juce::Colour (major ? 0x33a8b4c0 : 0x1aa8b4c0));
        g.drawVerticalLine ((int) x, (float) plot.getY(), (float) plot.getBottom());
    }

    g.setFont (juce::Font (12.0f));

    for (auto f : labelFreqs)
    {
        const auto x = xOfFreq (f);
        g.setColour (dark ? juce::Colour (0xffb9c2ca) : juce::Colour (0xff8794a1));
        g.drawText (Fusion::formatFreq (f),
                    juce::Rectangle<float> (x - 26.0f, (float) plot.getBottom() + 3.0f, 52.0f, 12.0f),
                    juce::Justification::centred);
    }

    /* --- 横向电平线（每 6 dB） --- */
    for (int db = (int) eqBot; db <= (int) eqTop; db += 6)
    {
        const auto y = yOfEq ((float) db);
        const bool zero = (db == 0);

        g.setColour (dark ? juce::Colour (zero ? 0x7ccfd6dc : 0x2bcfd6dc)
                          : juce::Colour (zero ? 0x66707c88 : 0x22707c88));
        g.drawHorizontalLine ((int) y, (float) plot.getX(), (float) plot.getRight());

        g.setColour (dark ? juce::Colour (zero ? 0xffe4e9ed : 0xffaeb8c0)
                          : juce::Colour (zero ? 0xff6b7885 : 0xff9aa4ae));
        g.setFont (juce::Font (12.0f, zero ? juce::Font::bold : juce::Font::plain));
        g.drawText ((db > 0 ? "+" : "") + juce::String (db),
                    juce::Rectangle<float> (2.0f, y - 6.0f, (float) plot.getX() - 6.0f, 12.0f),
                    juce::Justification::centredRight);
    }

    g.setColour (dark ? juce::Colour (0xffb9c2ca) : juce::Colour (0xff9aa4ae));
    g.setFont (juce::Font (11.5f));
    g.drawText ("dB", juce::Rectangle<float> (2.0f, (float) plot.getY() - 12.0f,
                                              (float) plot.getX() - 6.0f, 12.0f),
                juce::Justification::centredRight);
}

void SpectrumDisplay::drawSpectrum (juce::Graphics& g, juce::Rectangle<int> plot)
{
    const auto& spec = specLocal;
    const auto  n    = (int) spec.size();

    if (n < 2)
        return;

    juce::Path line, fill;
    bool started = false;

    for (int i = 0; i < n; ++i)
    {
        const auto t = (float) i / (float) (n - 1);
        const auto f = fMin * std::exp (t * logRatio);
        const auto x = xOfFreq (f);
        const auto y = yOfSpec (spec[(size_t) i]);

        if (! started)
        {
            line.startNewSubPath (x, y);
            fill.startNewSubPath (x, (float) plot.getBottom());
            fill.lineTo (x, y);
            started = true;
        }
        else
        {
            line.lineTo (x, y);
            fill.lineTo (x, y);
        }

        if (i == n - 1)
            fill.lineTo (x, (float) plot.getBottom());
    }

    if (! started)
        return;

    fill.closeSubPath();

    /* 频谱填充：从强调色到透明 */
    g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.22f), 0.0f, (float) plot.getY(),
                                             accent.withAlpha (0.02f), 0.0f, (float) plot.getBottom(),
                                             false));
    g.fillPath (fill);

    /* 频谱线：浅色皮肤中灰；深色面板转亮丝印色 */
    g.setColour ((skin != 0 ? juce::Colour (0xffd7dee4) : juce::Colour (0xff5f6f7d))
                     .withAlpha (0.85f));
    g.strokePath (line, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    /* 峰值保持线 */
    if (holdPeaks)
    {
        juce::Path pk;
        bool s2 = false;

        for (int i = 0; i < n; ++i)
        {
            const auto t = (float) i / (float) (n - 1);
            const auto x = xOfFreq (fMin * std::exp (t * logRatio));
            const auto y = yOfSpec (peaks[(size_t) i]);

            if (! s2) { pk.startNewSubPath (x, y); s2 = true; }
            else        pk.lineTo (x, y);
        }

        g.setColour (accent.withAlpha (0.55f));
        g.strokePath (pk, juce::PathStrokeType (1.0f));
    }
}

void SpectrumDisplay::drawCurves (juce::Graphics& g, juce::Rectangle<int> plot)
{
    const auto& snap  = curveLocal;                  // 本帧拷贝，绘制期间不会变
    const auto& freqs = processor.getCurveFreqs();
    const auto  n     = (int) freqs.size();

    /* --- 每段单独的曲线（细线，看清每段在做什么）
           鼠标停在机架某段上 / 正在拖动时，该段加粗并提到最上层 --- */
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int b = 0; b < Fusion::numBands; ++b)
        {
            if (! processor.getBandEnabled (b))
                continue;

            const bool emph = (b == highlightBand || b == activeBand);

            if ((pass == 1) != emph)
                continue;

            const auto& curve = snap.bandCurves[(size_t) b];
            const auto col = bandColour (b);

            juce::Path p;

            for (int i = 0; i < n; ++i)
            {
                const auto x = xOfFreq (freqs[(size_t) i]);
                const auto y = yOfEq (curve[(size_t) i]);

                if (i == 0) p.startNewSubPath (x, y);
                else        p.lineTo (x, y);
            }

            g.setColour (col.withAlpha (emph ? 0.95f : 0.30f));
            g.strokePath (p, juce::PathStrokeType (emph ? 2.0f : 1.2f,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        }
    }

    /* --- 总曲线 --- */
    juce::Path total;

    for (int i = 0; i < n; ++i)
    {
        const auto x = xOfFreq (freqs[(size_t) i]);
        const auto y = yOfEq (snap.total[(size_t) i]);

        if (i == 0) total.startNewSubPath (x, y);
        else        total.lineTo (x, y);
    }

    /* 阴影 */
    g.setColour (juce::Colours::black.withAlpha (0.10f));
    g.strokePath (total, juce::PathStrokeType (4.5f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded),
                  juce::AffineTransform::translation (0.0f, 1.2f));

    g.setColour (accent);
    g.strokePath (total, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    juce::ignoreUnused (plot);
}

/* 动态压缩可视化（干净模式、开了 DYN 的段）—— 每帧绘制，不走缓存层。
   Pro-Q 式"泪滴"：压缩对段的 delta 做线性缩放，所以作用区上/下界 =
   段曲线按 gr=−RANGE 缩放后的响应 ——
     · 提升段：泪滴从节点往下收尖，深度 = 增益 − RANGE；
     · 剪切段：作用区在曲线上方（压缩把剪切往回收）；
     · 远离段中心处自动收尖成点，不会像"整体减 RANGE"那样铺满全屏。
   亮线 = 实时曲线（随当前 GR 从静态曲线沉浮）；下缘虚线 = 最大作用界；
   作用区尖端两枚小三角 = 动态标记（Pro-Q 式）。 */
static float scaledBandDb (float curveDb, float grLin)
{
    const float lin = 1.0f + grLin * (std::pow (10.0f, curveDb / 20.0f) - 1.0f);
    return 20.0f * std::log10 (juce::jmax (1.0e-4f, lin));
}

void SpectrumDisplay::drawDynCurves (juce::Graphics& g, juce::Rectangle<int> plot)
{
    if (processor.getMode() != Fusion::Mode::Clean || ! snapshotsValid)
        return;

    const auto& freqs = processor.getCurveFreqs();
    const auto  n     = (int) freqs.size();

    g.saveState();
    g.reduceClipRegion (plot.expanded (3));

    for (int b = 0; b < Fusion::numBands; ++b)
    {
        if (! processor.getBandEnabled (b) || ! processor.getBandDynOn (b))
            continue;

        const auto& curve  = curveLocal.bandCurves[(size_t) b];
        const float rng    = processor.apvts.getRawParameterValue (IDs::bDynRng (b))->load();
        const float grLive = juce::jmax (0.0f, -processor.getBandDynGr (b));
        const auto  col    = bandColour (b);
        const bool  emph   = (b == highlightBand || b == activeBand);

        const float gZone = juce::Decibels::decibelsToGain (-rng);
        const float gNow  = juce::Decibels::decibelsToGain (-grLive);

        /* 作用区：静态曲线 ↔ 按 −RANGE 缩放后的曲线 */
        juce::Path zone;
        for (int i = 0; i < n; ++i)
        {
            const auto x = xOfFreq (freqs[(size_t) i]);
            const auto y = yOfEq (curve[(size_t) i]);

            if (i == 0) zone.startNewSubPath (x, y);
            else        zone.lineTo (x, y);
        }
        for (int i = n - 1; i >= 0; --i)
            zone.lineTo (xOfFreq (freqs[(size_t) i]),
                         yOfEq (scaledBandDb (curve[(size_t) i], gZone)));
        zone.closeSubPath();

        g.setColour (col.withAlpha (emph ? 0.20f : 0.13f));
        g.fillPath (zone);

        /* 作用区外缘虚线 */
        juce::Path border;
        for (int i = 0; i < n; ++i)
        {
            const auto x = xOfFreq (freqs[(size_t) i]);
            const auto y = yOfEq (scaledBandDb (curve[(size_t) i], gZone));

            if (i == 0) border.startNewSubPath (x, y);
            else        border.lineTo (x, y);
        }
        juce::Path dashed;
        const float dashes[2] = { 3.0f, 3.0f };
        juce::PathStrokeType (0.8f).createDashedStroke (dashed, border, dashes, 2);
        g.setColour (col.withAlpha (0.35f));
        g.strokePath (dashed, juce::PathStrokeType (0.8f));

        /* 实时曲线：压缩发生时从静态曲线向作用区方向沉浮。
           不设门限常绘 —— GR≈0 时亮线与静态曲线重合看不出，
           若加阈值会在门限附近反复开关造成高频闪烁 */
        {
            juce::Path dip;
            for (int i = 0; i < n; ++i)
            {
                const auto x = xOfFreq (freqs[(size_t) i]);
                const auto y = yOfEq (curve[(size_t) i]);

                if (i == 0) dip.startNewSubPath (x, y);
                else        dip.lineTo (x, y);
            }
            for (int i = n - 1; i >= 0; --i)
                dip.lineTo (xOfFreq (freqs[(size_t) i]),
                            yOfEq (scaledBandDb (curve[(size_t) i], gNow)));
            dip.closeSubPath();

            g.setColour (col.withAlpha (0.26f));
            g.fillPath (dip);

            juce::Path live;
            for (int i = 0; i < n; ++i)
            {
                const auto x = xOfFreq (freqs[(size_t) i]);
                const auto y = yOfEq (scaledBandDb (curve[(size_t) i], gNow));

                if (i == 0) live.startNewSubPath (x, y);
                else        live.lineTo (x, y);
            }
            g.setColour (col.withAlpha (emph ? 0.95f : 0.70f));
            g.strokePath (live, juce::PathStrokeType (emph ? 2.0f : 1.4f,
                                                      juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
        }

        /* 动态标记：作用区尖端（节点正下方/上方）两枚小三角（Pro-Q 式） */
        {
            const float f0     = processor.getBandFreq (b);
            const float tipDb  = scaledBandDb (bandCurveDbAt (b, f0), gZone);
            const float tx     = xOfFreq (f0);
            const float ty     = yOfEq (tipDb);
            const bool  below  = tipDb < processor.getBandGain (b);  // 泪滴朝下
            const float sgn    = below ? 1.0f : -1.0f;

            g.setColour (juce::Colours::white.withAlpha (0.9f));

            juce::Path tri;
            tri.addTriangle (tx - 5.0f, ty + sgn * 5.0f,
                             tx - 1.0f, ty + sgn * 5.0f,
                             tx - 3.0f, ty + sgn * 1.5f);
            g.fillPath (tri);

            juce::Path tri2;
            tri2.addTriangle (tx + 1.0f, ty + sgn * 1.5f,
                              tx + 5.0f, ty + sgn * 1.5f,
                              tx + 3.0f, ty + sgn * 5.0f);
            g.fillPath (tri2);
        }
    }

    g.restoreState();
}

/* 作用区尖端（两枚三角处）的屏幕坐标 —— 拖动命中的锚点 */
juce::Point<float> SpectrumDisplay::dynTipPos (int b) const
{
    const float f0  = processor.getBandFreq (b);
    const float rng = processor.apvts.getRawParameterValue (IDs::bDynRng (b))->load();
    const float gZone = juce::Decibels::decibelsToGain (-rng);
    const float tipDb = scaledBandDb (bandCurveDbAt (b, f0), gZone);

    return { xOfFreq (f0), yOfEq (tipDb) };
}

/* 作用区虚线整条线的命中测试：距线 12px 内的段（虚线比尖端三角好找得多） */
int SpectrumDisplay::dynZoneBandAt (juce::Point<float> pos) const
{
    if (processor.getMode() != Fusion::Mode::Clean || ! snapshotsValid)
        return -1;

    const auto& freqs = processor.getCurveFreqs();
    const int   n     = (int) freqs.size();

    int   best  = -1;
    float bestD = 12.0f;

    for (int b = 0; b < Fusion::numBands; ++b)
    {
        if (! processor.getBandEnabled (b) || ! processor.getBandDynOn (b))
            continue;

        const float rng   = processor.apvts.getRawParameterValue (IDs::bDynRng (b))->load();
        const float gZone = juce::Decibels::decibelsToGain (-rng);
        const auto& curve = curveLocal.bandCurves[(size_t) b];

        for (int i = 0; i < n; i += 3)              // 曲线点够密，粗采样即可
        {
            const auto p = juce::Point<float> (
                xOfFreq (freqs[(size_t) i]),
                yOfEq (scaledBandDb (curve[(size_t) i], gZone)));
            const float d = p.getDistanceFrom (pos);

            if (d < bestD) { bestD = d; best = b; }
        }
    }

    return best;
}

/* 由尖端目标响应反解 RANGE（dB）；无法求解时返回 -1 */
static float rngFromTipDb (float curveDb0, float tipDb)
{
    const float cl = std::pow (10.0f, curveDb0 / 20.0f);
    const float tl = std::pow (10.0f, tipDb   / 20.0f);
    const float denom = cl - 1.0f;

    if (std::abs (denom) < 0.02f)          // 段增益≈0：delta 没有，拖不出范围
        return -1.0f;

    const float gr = (tl - 1.0f) / denom;  // 需要的缩放系数

    if (gr <= 0.0f)                        // 拖过头（如提升段拖到 0 dB 以上）
        return 0.0f;

    return juce::jlimit (0.0f, 24.0f, -juce::Decibels::gainToDecibels (gr, -60.0f));
}

void SpectrumDisplay::drawNodes (juce::Graphics& g, juce::Rectangle<int> plot)
{
    for (int i = 0; i < Fusion::numBands; ++i)
    {
        if (! Fusion::isBandActive (processor.getMode(), i))
            continue;                      // SSL 4 段模式：不画被屏蔽频段的节点

        const bool on = processor.getBandEnabled (i);
        const auto x  = xOfFreq (processor.getBandFreq (i));
        const auto y  = nodeYOf (i);

        const bool hot = (i == activeBand || i == hoverBand || i == selectedBand
                          || i == highlightBand);
        const auto r   = hot ? 7.0f : 5.5f;
        const auto col = bandColour (i);

        if (hot)
        {
            g.setColour (col.withAlpha (0.26f));
            g.fillEllipse (x - r - 4.0f, y - r - 4.0f, (r + 4.0f) * 2.0f, (r + 4.0f) * 2.0f);
        }

        if (on)
        {
            g.setColour (col);
            g.fillEllipse (x - r, y - r, r * 2.0f, r * 2.0f);
            g.setColour (juce::Colours::white);
            g.drawEllipse (x - r, y - r, r * 2.0f, r * 2.0f, 1.6f);
            g.setColour (col.darker (0.45f));
            g.drawEllipse (x - r, y - r, r * 2.0f, r * 2.0f, 0.8f);
        }
        else
        {
            g.setColour (col.withAlpha (0.45f));
            g.drawEllipse (x - r, y - r, r * 2.0f, r * 2.0f, 1.4f);
        }

        /* 段号；声道模式非 Stereo 时显示声道字母（Pro-Q 式 M/S/L/R 徽标） */
        const int cm = processor.getBandChan (i);
        const auto badge = cm == 1 ? "L" : cm == 2 ? "R"
                         : cm == 3 ? "M" : cm == 4 ? "S" : "";
        g.setColour (on ? juce::Colours::white : col.withAlpha (0.85f));
        g.setFont (juce::Font (7.5f, juce::Font::bold));
        g.drawText (cm != 0 ? juce::String (badge) : juce::String (i + 1),
                    juce::Rectangle<float> (x - r, y - r, r * 2.0f, r * 2.0f),
                    juce::Justification::centred);
    }

    /* 悬停/拖动/选中/监听的频点上方弹出信息浮窗（Pro-Q 式） */
    drawHUD (g);

    /* 拖动时不画十字参考线 —— 保持画面干净，读数条已提供精确数值 */
    juce::ignoreUnused (plot);
}

/* ============================ Pro-Q 式信息浮窗 ============================ */

/* 浮窗跟随的频段：监听中的优先，其次正在拖动 / 悬停 / 选中的 */
int SpectrumDisplay::hudBand() const
{
    if (rngTipDrag)        return -1;             // 拖泪滴尖端（RANGE）：只看黄线动，不联动浮窗
    if (processor.getListenBand() >= 0) return processor.getListenBand();
    if (activeBand   >= 0) return activeBand;
    if (hoverBand    >= 0) return hoverBand;
    return -1;                        // 选中状态不再唤出浮窗（杜绝误触）
}

juce::Rectangle<float> SpectrumDisplay::hudRect (int b) const
{
    const auto x  = xOfFreq (processor.getBandFreq (b));
    const auto y  = nodeYOf (b);

    /* 干净模式：DYN 行常显；四个压缩旋钮随 hudDynVis 淡入展开 */
    const bool clean = processor.getMode() == Fusion::Mode::Clean;
    const float w = clean ? 300.0f : 220.0f;          // 字号放大后加宽
    const float h = clean ? 108.0f + 56.0f * hudDynVis : 80.0f;
    float bx = juce::jlimit (plotX + 4.0f, plotX + plotW - w - 4.0f, x - w * 0.5f);

    /* 上/下放置带滞回：上方放不下才落到下方；一旦在下方就粘住，
       直到下方也放不下才回上方 —— 避免频点在临界线附近时每帧翻转闪烁。
       两边都放不下时保持当前边不翻转，最后整体钳位进绘图区 */
    const bool fitsAbove = (y - 18.0f - h >= plotY + 4.0f);
    const bool fitsBelow = (y + 18.0f + h <= plotY + plotH - 4.0f);
    bool below = (hudBelowBand == b);

    if (! below && ! fitsAbove && fitsBelow)
    {
        below = true;
        hudBelowBand = b;
    }
    else if (below && ! fitsBelow && fitsAbove)
    {
        below = false;
        hudBelowBand = -1;
    }

    float by = below ? y + 18.0f : y - 18.0f - h;

    if (plotY + plotH - h - 4.0f > plotY + 4.0f)         // 绘图区放得下才钳位（防退化区间）
        by = juce::jlimit (plotY + 4.0f, plotY + plotH - h - 4.0f, by);

    return { bx, by, w, h };
}

juce::Rectangle<float> SpectrumDisplay::hudHeadRect (int b) const
{
    const auto r = hudRect (b);
    /* 耳机按钮在 × 左侧，两者之间留出间隙 ——
       原先耳机 [右-38, 右-12]、× [右-22, 右-5] 重叠了 10px，图标糊在一起 */
    return { r.getRight() - 48.0f, r.getY() + 6.0f, 26.0f, 26.0f };
}

/* DYN 行（干净模式）：左上胶囊开关 + 实时 GR 读数 */
juce::Rectangle<float> SpectrumDisplay::hudDynRect (int b) const
{
    const auto r = hudRect (b);
    return { r.getX() + 15.0f, r.getY() + 53.0f, 66.0f, 24.0f };   // DYN 胶囊同步放大
}

/* 动态压缩小旋钮（干净模式）：k = 0 THR / 1 RNG / 2 ATT / 3 REL
   旋钮整体上移（原 +74）：贴近读数行、远离浮窗底缘 —— 黄色虚线
   常从浮窗下部穿过，太低会被误当成拖黄线 */
juce::Rectangle<float> SpectrumDisplay::hudKnobRect (int b, int k) const
{
    const auto r = hudRect (b);
    const float cellW = (r.getWidth() - 16.0f) / 4.0f;
    const float cx = r.getX() + 8.0f + cellW * ((float) k + 0.5f);
    return { cx - 15.0f, r.getY() + 57.0f, 30.0f, 30.0f };
}

/* 动态压缩参数辅助：k = 0 THR / 1 RNG / 2 ATT / 3 REL */
static juce::String dynParamId (int b, int k)
{
    switch (k)
    {
        case 0:  return IDs::bDynThr (b);
        case 1:  return IDs::bDynRng (b);
        case 2:  return IDs::bDynAtt (b);
        default: return IDs::bDynRel (b);
    }
}

static juce::String dynParamText (FusionEQAudioProcessor& p, int b, int k)
{
    static const char* nm[4] = { "THR", "RNG", "ATT", "REL" };
    static const char* un[4] = { " dB", " dB", " ms", " ms" };

    if (auto* prm = p.apvts.getParameter (dynParamId (b, k)))
    {
        const float v = prm->getNormalisableRange().convertFrom0to1 (prm->getValue());
        return juce::String (nm[k]) + " " + juce::String (v, 1) + un[k];
    }

    return {};
}

/* Pro-Q 式声道模式小图标：直接用模式色的文字标记（肥波式 L+R / L / R / M / S），
   比图形更直白 —— 默认 Stereo 档一眼就能认出来
   cm: 0=Stereo 1=Left 2=Right 3=Mid 4=Side */
static std::unique_ptr<juce::Drawable> makeChanIcon (int cm)
{
    static const juce::String modeTxt[5] = { "L+R", "L", "R", "M", "S" };
    static const juce::Colour dotCols[5] = { juce::Colour (0xffffffff), juce::Colour (0xffe5484d),
                                             juce::Colour (0xffe8863a), juce::Colour (0xff46c46a),
                                             juce::Colour (0xffa879ff) };
    const auto col = dotCols[juce::jlimit (0, 4, cm)];

    /* 文字转路径（矢量图标，菜单缩放不失真） */
    juce::GlyphArrangement ga;
    ga.addFittedText (juce::Font (12.5f, juce::Font::bold), modeTxt[juce::jlimit (0, 4, cm)],
                      0.0f, 0.0f, 24.0f, 12.0f, juce::Justification::centred, 1);

    juce::Path p;
    ga.createPath (p);

    auto d = std::make_unique<juce::DrawablePath>();
    d->setPath (p);
    d->setFill (juce::FillType (col));
    return d;
}

void SpectrumDisplay::drawHUD (juce::Graphics& g)
{
    int b = hudBand();

    /* 淡出期间 hudBand() 已归 -1：继续绘制上一帧的段直到 alpha 走完 */
    if (b < 0)
        b = hudShowBand;

    if (b < 0 || hudAlpha <= 0.02f)
        return;

    const auto r = hudRect (b);
    const float a = hudAlpha;                    // 淡出透明度

    /* 底板：投影 + 深色圆角面板 + 细描边 */
    g.setColour (juce::Colours::black.withAlpha (0.35f * a));
    g.fillRoundedRectangle (r.translated (0.0f, 2.0f), 7.0f);
    g.setColour (juce::Colour (0xe822282e).withAlpha (0.60f * a));   // 半透明：不挡住底下的曲线
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (juce::Colours::white.withAlpha (0.10f * a));
    g.drawRoundedRectangle (r, 7.0f, 1.0f);

    /* 耳机开关按钮：监听中红色（Pro-Q 式），平时深灰 */
    const bool listening = (processor.getListenBand() == b);
    const auto hr = hudHeadRect (b);

    g.setColour (listening ? juce::Colour (0xffe5484d).withAlpha (a)
                           : juce::Colour (0xff333b43).withAlpha (a));
    g.fillRoundedRectangle (hr, 5.0f);

    if (listening)
    {
        g.setColour (juce::Colour (0xffe5484d).withAlpha (0.30f * a));
        g.fillRoundedRectangle (hr.expanded (4.0f), 7.0f);
    }

    auto icon = Fusion::makeHeadphoneIcon (
        listening ? juce::Colours::white.withAlpha (a)
                  : juce::Colour (0xffc7d0d8).withAlpha (a));
    icon->draw (g, 1.0f,
                juce::AffineTransform::translation (hr.getCentreX() - 13.0f,
                                                    hr.getCentreY() - 8.0f));

    /* 不设关闭按钮：浮窗本就会在鼠标离开 0.5s 后自动淡出，
       手动关闭既多余、又容易与耳机按钮误触 */

    /* 读数：频率 / 增益 / Q —— 拖动时实时刷新 */
    const auto textCol  = juce::Colours::white.withAlpha (a);
    const auto labelCol = juce::Colour (0xffaeb9c2).withAlpha (a);
    const auto tx = juce::Rectangle<float> (r.getX() + 11.0f, 0.0f, 104.0f, 0.0f);

    g.setColour (textCol);
    g.setFont (juce::Font (17.5f, juce::Font::bold));
    g.drawText (Fusion::formatFreq (processor.getBandFreq (b)),
                tx.withY (r.getY() + 9.0f).withHeight (21.0f),
                juce::Justification::centredLeft);

    g.setColour (labelCol);
    g.setFont (juce::Font (15.0f));
    /* 增益与 Q 合并为一行（下方空间让给 DYN 行与压缩旋钮：整体上移、
       远离从浮窗下部穿过的黄色虚线）。低切/高切段没有增益，改显共振峰电平。 */
    const bool cut = isCutBand (b);
    /* 低切/高切段：竖轴是共振（见 cutQToDispDb），按住滚轮改的是斜率，
       所以这里把斜率显示出来（Q 在下方旋钮上已经能看到）。 */
    const juce::String readout2 =
        cut ? (juce::String ("RES ") + Fusion::formatGain (
                   cutQToDispDb (processor.getBandQ (b)))
                   + "  ·  " + Fusion::cutSlopeNames()[(size_t) Fusion::cutSlopeIndex (
                                        processor.getBandSlope (b))])
             : (Fusion::formatGain (processor.getBandGain (b))
                + "  ·  Q " + Fusion::formatQ (processor.getBandQ (b)));

    g.drawText (readout2,
                tx.withY (r.getY() + 31.0f).withHeight (19.0f),
                juce::Justification::centredLeft);

    /* DYN 行（干净模式）：胶囊开关 + 实时 GR + 四个压缩小旋钮 */
    if (processor.getMode() == Fusion::Mode::Clean)
    {
        const bool dyn = processor.getBandDynOn (b);
        const auto dr  = hudDynRect (b);

        /* DYN 胶囊开关 */
        g.setColour ((dyn ? juce::Colour (0xff2f9e6a) : juce::Colour (0xff333b43)).withAlpha (a));
        g.fillRoundedRectangle (dr, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (a));
        g.setFont (juce::Font (14.0f, juce::Font::bold));
        g.drawText ("DYN", dr, juce::Justification::centred);

        /* 右侧读数：拖旋钮时显示该参数实时值，否则显示实时 GR */
        const auto readoutR = juce::Rectangle<float> (dr.getRight() + 8.0f, dr.getY(),
                                                      r.getRight() - dr.getRight() - 14.0f,
                                                      dr.getHeight());
        g.setFont (juce::Font (15.0f));

        if (dynKnobDrag && dynKnobBand == b)
        {
            g.setColour (juce::Colours::white.withAlpha (a));
            g.drawText (dynParamText (processor, b, dynKnobIdx),
                        readoutR, juce::Justification::centredRight);
        }
        else if (dyn && processor.getBandDynGr (b) < -0.1f)
        {
            g.setColour (juce::Colour (0xffffb347).withAlpha (a));
            g.drawText ("GR " + juce::String (processor.getBandDynGr (b), 1) + " dB",
                        readoutR, juce::Justification::centredRight);
        }

        /* 四个迷你旋钮：DYN 打开后淡入，关闭时淡出消失 */
        if (hudDynVis > 0.02f)
        {
            static const char* kn[4] = { "THR", "RNG", "ATT", "REL" };
            const auto kCol = bandColour (b);
            const float ka  = a * hudDynVis;      // 旋钮区独立淡入透明度

            for (int k = 0; k < 4; ++k)
            {
                const auto kr   = hudKnobRect (b, k);
                const auto c    = kr.getCentre();
                const float rad = kr.getWidth() * 0.5f;
                const float norm = [&]
                {
                    if (auto* prm = processor.apvts.getParameter (dynParamId (b, k)))
                        return prm->getValue();
                    return 0.0f;
                }();
                const float ang  = juce::degreesToRadians (-135.0f + 270.0f * norm);
                const bool  hot  = dynKnobDrag && dynKnobBand == b && dynKnobIdx == k;

                /* 钮面 */
                g.setColour ((hot ? juce::Colour (0xff4a545e) : juce::Colour (0xff3a444d)).withAlpha (ka));
                g.fillEllipse (c.x - rad * 0.66f, c.y - rad * 0.66f, rad * 1.32f, rad * 1.32f);
                g.setColour (juce::Colours::black.withAlpha (0.35f * ka));
                g.drawEllipse (c.x - rad * 0.66f, c.y - rad * 0.66f, rad * 1.32f, rad * 1.32f, 0.8f);

                /* 轨迹弧 + 数值弧 */
                juce::Path track;
                track.addArc (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f,
                              juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
                g.setColour (juce::Colours::white.withAlpha (0.14f * ka));
                g.strokePath (track, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));

                if (norm > 0.002f)
                {
                    juce::Path val;
                    val.addArc (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f,
                                juce::degreesToRadians (-135.0f),
                                juce::degreesToRadians (-135.0f + 270.0f * norm), true);
                    g.setColour ((dyn ? kCol : kCol.withAlpha (0.45f)).withAlpha (ka));
                    g.strokePath (val, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                                                             juce::PathStrokeType::rounded));
                }

                /* 指针 */
                const float sa = std::sin (ang), ca = std::cos (ang);
                g.setColour (juce::Colours::white.withAlpha ((hot ? 1.0f : 0.85f) * ka));
                g.drawLine ({ c.x - sa * rad * 0.26f, c.y + ca * rad * 0.26f,
                              c.x + sa * rad * 0.74f, c.y - ca * rad * 0.74f }, 2.0f);

                /* 名称 */
                g.setColour (juce::Colour (0xffaeb9c2).withAlpha (ka));
                g.setFont (juce::Font (12.0f, juce::Font::bold));
                g.drawText (kn[k], juce::Rectangle<float> (kr.getX() - 10.0f, kr.getBottom() + 2.0f,
                                                           kr.getWidth() + 20.0f, 10.0f),
                            juce::Justification::centred);
            }
        }
    }
}

/* ============================ 命中测试 ============================ */

/* 频段自身曲线在 freq 处的响应（dB），从 60Hz 快照里线性插值 */
float SpectrumDisplay::bandCurveDbAt (int b, float freq) const
{
    const auto& c  = curveLocal.bandCurves[(size_t) b];
    const auto& cf = processor.getCurveFreqs();
    const int   n  = (int) c.size();
    juce::ignoreUnused (cf);

    const float t = (std::log (juce::jlimit (Fusion::fMin, Fusion::fMax, freq) / Fusion::fMin)
                     / std::log (Fusion::fMax / Fusion::fMin)) * (float) (n - 1);
    const int   i0 = juce::jlimit (0, n - 2, (int) t);
    const float fr = t - (float) i0;

    return c[(size_t) i0] * (1.0f - fr) + c[(size_t) i0 + 1] * fr;
}

/* ---- 低切 / 高切段：竖直方向不是增益而是共振 Q ----
   二阶高通/低通在截止点附近的共振峰：L = Q / sqrt(1 - 1/(4Q²))
   → db = 20·log10(L)（Q = 1/√2 正好 0 dB，Q 越大隆起越高）。
   0 dB 以下（Q < 1/√2，曲线单调无隆起）线性映射到 Q 0.5，让整条竖轴都可用。
   绘制与拖拽共用这对互为反函数的换算 → 节点严格跟手。 */
float SpectrumDisplay::cutQToDispDb (float q) noexcept
{
    if (q <= 0.70711f)
        return juce::jlimit (-18.0f, 0.0f,
                             (q - 0.5f) / (0.70711f - 0.5f) * 18.0f - 18.0f);

    const double q2 = (double) q * (double) q;
    const double L  = (double) q / std::sqrt (juce::jmax (1e-9, 1.0 - 1.0 / (4.0 * q2)));
    return (float) (20.0 * std::log10 (L));
}

float SpectrumDisplay::cutDispDbToQ (float db) noexcept
{
    if (db <= 0.0f)
        return juce::jlimit (0.5f, 0.70711f,
                             0.5f + (db + 18.0f) / 18.0f * (0.70711f - 0.5f));

    /* L = Q / sqrt(1 - 1/(4Q²)) 反解：4x² - 4L²x + L² = 0（x = Q²），取大根 */
    const double L  = std::pow (10.0, (double) db / 20.0);
    const double L2 = L * L;
    const double x  = (L2 + L * std::sqrt (juce::jmax (0.0, L2 - 1.0))) / 2.0;
    return (float) std::sqrt (juce::jmax (0.0, x));
}

bool SpectrumDisplay::isCutBand (int b) const
{
    const auto t = (Fusion::BandType) processor.getBandType (b);
    return t == Fusion::BandType::LowCut || t == Fusion::BandType::HighCut;
}

/* 光标纵向位置 → 共振 Q：直接取该 dB 对应的 Q（与绘制同一函数，节点 1:1 跟手） */
void SpectrumDisplay::dragBandQFromY (int b, float yPos)
{
    if (auto* pq = dragQ != nullptr ? dragQ
                                     : processor.apvts.getParameter (IDs::bQ (b)))
    {
        const float q = juce::jlimit (Fusion::qMin, Fusion::qMax,
                                      cutDispDbToQ (gainOfY (yPos)));
        pq->setValueNotifyingHost (pq->getNormalisableRange().convertTo0to1 (q));
    }
}

/* 节点纵轴位置：低切/高切 = 共振 Q 的竖轴表示（随 Q 上下移动），
   其它类型画在增益参数的位置 */
float SpectrumDisplay::nodeYOf (int b) const
{
    const bool on = processor.getBandEnabled (b);
    const float f = processor.getBandFreq (b);

    if (on)
    {
        const auto t = (Fusion::BandType) processor.getBandType (b);

        if (t == Fusion::BandType::LowCut || t == Fusion::BandType::HighCut)
            return yOfEq (cutQToDispDb (processor.getBandQ (b)));
    }

    return yOfEq (on ? processor.getBandGain (b) : 0.0f);
}

int SpectrumDisplay::bandAt (juce::Point<float> pos, float radius) const
{
    int   best  = -1;
    float bestD = radius;

    for (int i = 0; i < Fusion::numBands; ++i)
    {
        if (! Fusion::isBandActive (processor.getMode(), i))
            continue;                      // SSL 4 段模式：隐藏频段不可命中

        const auto p  = juce::Point<float> (xOfFreq (processor.getBandFreq (i)),
                                            nodeYOf (i));
        const auto d  = p.getDistanceFrom (pos);

        if (d < bestD) { bestD = d; best = i; }
    }

    return best;
}

juce::RangedAudioParameter* SpectrumDisplay::freqParam (int i)
{
    return processor.apvts.getParameter (IDs::bFreq (i));
}

juce::RangedAudioParameter* SpectrumDisplay::gainParam (int i)
{
    return processor.apvts.getParameter (IDs::bGain (i));
}

void SpectrumDisplay::beginEdit (int band)
{
    endEdit();

    dragFreq = freqParam (band);
    dragGain = gainParam (band);
    dragQ    = processor.apvts.getParameter (IDs::bQ (band));

    if (dragFreq != nullptr) dragFreq->beginChangeGesture();
    if (dragGain != nullptr) dragGain->beginChangeGesture();
    if (dragQ    != nullptr) dragQ->beginChangeGesture();
    gestureActive = true;
}

void SpectrumDisplay::endEdit()
{
    if (! gestureActive)
        return;

    if (dragFreq != nullptr) dragFreq->endChangeGesture();
    if (dragGain != nullptr) dragGain->endChangeGesture();
    if (dragQ    != nullptr) dragQ->endChangeGesture();

    dragFreq = nullptr;
    dragGain = nullptr;
    dragQ    = nullptr;
    gestureActive = false;
}

/* ============================ 鼠标交互 ============================ */
void SpectrumDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (bypassVisual)          // BYPASS：频谱只读，任何点击/拖拽都不生效
        return;

    const auto pos = e.position;
    const bool  inside = (pos.x >= plotX && pos.x <= plotX + plotW
                       && pos.y >= plotY && pos.y <= plotY + plotH);

    if (! inside)
        return;

    /* 压缩作用区虚线（整条线 + 尖端）：按住垂直拖动 = RANGE（Pro-Q 式）。
       虚线好抓（12px 缓冲）；但正压在频点核心（8px）时仍优先抓频点，
       避免虚线和节点重合时抢走拖频点的手势。
       悬浮框区域内一律不抓黄线 —— 黄虚线常从浮窗下部（旋钮行）穿过，
       否则点旋钮会被抢成拖黄线；浮窗内的命中全部让给浮窗控件 */
    if (e.mods.isLeftButtonDown() && ! e.mods.isAltDown()
        && processor.getMode() == Fusion::Mode::Clean)
    {
        const int zb = dynZoneBandAt (pos);

        const int hbNow = hudBand();
        const bool overHud = hbNow >= 0 && hudAlpha > 0.5f
                             && hudRect (hbNow).contains (pos);

        if (zb >= 0 && bandAt (pos, 8.0f) < 0 && ! overHud)
        {
            rngTipBand  = zb;
            rngTipParam = processor.apvts.getParameter (IDs::bDynRng (zb));
            rngTipDrag  = true;
            selectedBand = zb;
            hudPinBand  = -1;                  // 抓黄线瞬间甩掉浮窗，拖动期间不再弹出
            hoverBand   = -1;

            if (rngTipParam != nullptr)
                rngTipParam->beginChangeGesture();

            repaint();
            return;
        }
    }

    /* 信息浮窗按钮：耳机 = 按住监听（按住期间照常进入拖动手势 ——
       可以边听边拖频点扫频、滚轮 Q），松开即停；
       × = 停止监听并取消选中（悬浮框淡出后不可点） */
    if (e.mods.isLeftButtonDown() && ! e.mods.isAltDown() && hudAlpha > 0.5f)
    {
        const int hb = hudBand();

        if (hb >= 0)
        {
            if (hudHeadRect (hb).contains (pos))
            {
                processor.setListenBand (hb);
                activeBand   = hb;
                selectedBand = hb;
                holdListen   = true;

                /* 相对扫频：按住时频点原地不动，鼠标移动超过阈值后才
                   以"按住点为原点"的偏移量继续拖动 —— 不会一按就跳 */
                holdDragActive = false;
                holdPressPos   = pos;
                holdStartFreq  = processor.getBandFreq (hb);
                holdStartGain  = processor.getBandGain (hb);

                beginEdit (hb);
                repaint();
                return;
            }

            /* 动态压缩小旋钮（干净模式）：按下进入垂直拖动（Shift 微调），
               旋钮区收起（hudDynVis 未展开）时不可命中 */
            if (processor.getMode() == Fusion::Mode::Clean && hudDynVis > 0.5f)
            {
                for (int k = 0; k < 4; ++k)
                {
                    if (hudKnobRect (hb, k).expanded (4.0f).contains (pos))
                    {
                        dynKnobBand     = hb;
                        dynKnobIdx      = k;
                        dynKnobStartY   = pos.y;
                        dynDragParam    = processor.apvts.getParameter (dynParamId (hb, k));
                        dynKnobStartVal = dynDragParam != nullptr ? dynDragParam->getValue() : 0.0f;
                        dynKnobDrag     = true;

                        if (dynDragParam != nullptr)
                            dynDragParam->beginChangeGesture();

                        repaint();
                        return;
                    }
                }
            }

            /* DYN 胶囊开关（干净模式）：切换该段动态压缩 */
            if (processor.getMode() == Fusion::Mode::Clean
                && hudDynRect (hb).contains (pos))
            {
                if (auto* p = dynamic_cast<juce::AudioParameterBool*> (
                        processor.apvts.getParameter (IDs::bDynOn (hb))))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (p->get() > 0.5f ? 0.0f : 1.0f);
                    p->endChangeGesture();
                }

                repaint();
                return;
            }

            /* 悬浮窗主体内（非按钮/旋钮处）：只保持选中当前频点，
               不落到下面的节点抓取 / 空白生成逻辑 —— 避免误触其它频点 */
            if (hudRect (hb).contains (pos))
            {
                selectedBand = hb;
                repaint();
                return;
            }
        }
    }

    /* 右键：弹出 EQ 类型菜单（作用于光标处的频点；无命中则选横向最近的一段；
       悬浮窗内右键 = 直接作用于悬浮窗跟随的频点） */
    if (e.mods.isPopupMenu())
    {
        int band = bandAt (pos);

        if (hudAlpha > 0.5f && hudBand() >= 0
            && hudRect (hudBand()).contains (pos))
            band = hudBand();

        if (band < 0)
        {
            float best = 1.0e9f;

            for (int i = 0; i < Fusion::numBands; ++i)
            {
                if (! Fusion::isBandActive (processor.getMode(), i))
                    continue;

                const auto d = std::abs (xOfFreq (processor.getBandFreq (i)) - pos.x);
                if (d < best) { best = d; band = i; }
            }
        }

        if (band >= 0)
        {
            selectedBand = band;
            repaint();

            const auto cur = juce::jlimit (0, (int) Fusion::BandType::Count - 1,
                                           processor.getBandType (band));

            juce::PopupMenu m;
            m.addSectionHeader (Fusion::U8 ("EQ TYPE \u00b7 B") + juce::String (band + 1));

            /* EQ TYPE：默认折叠的子菜单，内含 9 种类型；SSL 模式按原版收敛 */
            const bool sslMode = processor.getMode() == Fusion::Mode::SSL;

            juce::Array<int> allowed;
            if (sslMode)
            {
                /* SSL bandMask 0x2d → LF=0 / LMF=2 / HMF=3 / HF=5 */
                if (band == 0)       allowed = { 0, 1 };   // LF：搁架 / 钟形
                else if (band == 5)  allowed = { 0, 3 };   // HF：BELL / HIGH SHELF
                else                 allowed = { 0 };      // LMF/HMF：固定 BELL
            }

            juce::PopupMenu typeMenu;
            for (int t = 0; t < (int) Fusion::BandType::Count; ++t)
            {
                if (sslMode && ! allowed.contains (t))
                    continue;

                typeMenu.addItem (t + 1, Fusion::TYPE_LABELS()[(size_t) t], true, t == cur,
                                  Fusion::makeTypeIcon (t, juce::Colour (0xffe8eef4)));
            }
            m.addSubMenu (Fusion::U8 ("EQ TYPE"), typeMenu,
                          true, Fusion::makeTypeIcon (cur, juce::Colour (0xffe8eef4)));

            /* CHANNEL（Pro-Q 式，仅干净模式）：Stereo / Left / Right / Mid / Side */
            if (processor.getMode() == Fusion::Mode::Clean)
            {
                static const juce::String chanNames[5] = { "Stereo", "Left", "Right", "Mid", "Side" };
                static const juce::Colour chanCols [5] = { juce::Colour (0xffffffff), juce::Colour (0xffe5484d),
                                                           juce::Colour (0xffe8863a), juce::Colour (0xff46c46a),
                                                           juce::Colour (0xffa879ff) };
                const int  curChan = processor.getBandChan (band);
                /* Mono 轨（主输入 < 2 声道）没有立体声拓扑：
                   Left / Right / Mid / Side 全部置灰，只允许 Stereo */
                const bool stereo = processor.isStereoInput();

                juce::PopupMenu chanMenu;

                for (int cm = 0; cm < 5; ++cm)
                    chanMenu.addItem (1101 + cm, chanNames[cm], (cm == 0 || stereo), cm == curChan,
                                      makeChanIcon (cm));

                m.addSubMenu (Fusion::U8 ("CHANNEL"), chanMenu, true,
                              makeChanIcon (curChan));
            }

            /* 动态压缩不再进右键菜单 —— 悬浮框里已有 DYN 开关 + 四旋钮 */

            m.showMenuAsync (juce::PopupMenu::Options(),
                [this, band] (int result)
                {
                    /* 1101..1105 = 声道模式 */
                    if (result >= 1101 && result <= 1105)
                    {
                        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (
                                processor.apvts.getParameter (IDs::bChan (band))))
                        {
                            p->beginChangeGesture();
                            p->setValueNotifyingHost (p->convertTo0to1 ((float) (result - 1101)));
                            p->endChangeGesture();
                        }
                        return;
                    }

                    if (result <= 0 || result > (int) Fusion::BandType::Count)
                        return;

                    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (
                            processor.apvts.getParameter (IDs::bType (band))))
                    {
                        p->beginChangeGesture();
                        p->setValueNotifyingHost (p->convertTo0to1 ((float) (result - 1)));
                        p->endChangeGesture();
                    }
                });

        }

        return;
    }

    /* Alt + 左键：把命中的频点恢复为当前模式的默认设置并关闭该段 */
    if (e.mods.isAltDown())
    {
        const int hit = bandAt (pos);

        if (hit >= 0)
        {
            const auto& def = Fusion::getSpec (processor.getMode()).bands[(size_t) hit];

            auto setParam = [this] (const juce::String& id, float real)
            {
                if (auto* p = processor.apvts.getParameter (id))
                    p->setValueNotifyingHost (
                        p->getNormalisableRange().convertTo0to1 (real));
            };

            setParam (IDs::bFreq (hit), def.freq);
            setParam (IDs::bGain (hit), def.gainDb);
            setParam (IDs::bQ    (hit), def.q);
            setParam (IDs::bOn   (hit), 0.0f);

            selectedBand = hit;
            repaint();
        }

        return;
    }

    int hit = bandAt (pos);

    if (hit < 0)
    {
        /* Pro-Q 式：空白处按下 = 就在这个位置“生成”一个控制点。
           六段是固定的，所以按下面的优先级挑一个段来承载：
             1) 已关闭的段（本来就闲置）
             2) 增益 ≈ 0 dB 的段（听感上等于没在用，挪走不会破坏已有设置）
             3) 都没有 → 只选中横向最近的一段，不擅自改动参数
           横向距离作为次要判据，避免抢走远处的段。 */
        int   offBand  = -1, zeroBand = -1, nearest = -1;
        float bestOff  = 1.0e9f, bestZero = 1.0e9f, bestNear = 1.0e9f;

        for (int i = 0; i < Fusion::numBands; ++i)
        {
            if (! Fusion::isBandActive (processor.getMode(), i))
                continue;                  // SSL 4 段模式：被屏蔽频段不参与"生成节点"

            const auto d = std::abs (xOfFreq (processor.getBandFreq (i)) - pos.x);

            if (d < bestNear) { bestNear = d; nearest = i; }

            if (! processor.getBandEnabled (i))
            {
                if (d < bestOff) { bestOff = d; offBand = i; }
            }
            else if (std::abs (processor.getBandGain (i)) < 0.2f)
            {
                if (d < bestZero) { bestZero = d; zeroBand = i; }
            }
        }

        const int reuse = (offBand >= 0 ? offBand : zeroBand);

        /* SSL 模式（原版 4000 逻辑）：不允许在空白处直接生成频点，
           只能拖动已有的频点 —— 空白按下仅选中横向最近的一段 */
        const bool sslMode = processor.getMode() == Fusion::Mode::SSL;

        if (reuse >= 0 && ! sslMode)
        {
            activeBand   = reuse;
            selectedBand = reuse;
            beginEdit (reuse);                       // 先开手势，后面的写入才是一个可撤销的动作

            if (auto* on = processor.apvts.getParameter (IDs::bOn (reuse)))
                on->setValueNotifyingHost (1.0f);

            if (auto* p = freqParam (reuse))
                p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 (
                    juce::jlimit (Fusion::fMin, 20000.0f, freqOfX (pos.x))));

            if (auto* p = gainParam (reuse))
                p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 (gainOfY (pos.y)));

            repaint();
            return;                                   // 手势保持开启，接着拖动即可直接塑形
        }

        hit = nearest;
    }

    if (hit < 0)
        return;

    activeBand   = hit;
    selectedBand = hit;
    beginEdit (hit);
    repaint();
}

void SpectrumDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (bypassVisual)          // BYPASS：拖拽一律不生效
        return;

    /* 泪滴尖端拖动：尖端跟随鼠标 y → 反解 RANGE */
    if (rngTipDrag && rngTipParam != nullptr)
    {
        const float curveDb0 = bandCurveDbAt (rngTipBand,
                                              processor.getBandFreq (rngTipBand));
        const float rng = rngFromTipDb (curveDb0, gainOfY (e.position.y));

        if (rng >= 0.0f)
            rngTipParam->setValueNotifyingHost (
                rngTipParam->getNormalisableRange().convertTo0to1 (rng));

        repaint();
        return;
    }

    /* 悬浮窗压缩小旋钮：垂直拖动（向上加值），Shift = 1/6 精度 */
    if (dynKnobDrag && dynDragParam != nullptr)
    {
        const float perPx = e.mods.isShiftDown() ? 1.0f / 960.0f : 1.0f / 160.0f;
        const float v = juce::jlimit (0.0f, 1.0f,
                                      dynKnobStartVal + (dynKnobStartY - e.position.y) * perPx);
        dynDragParam->setValueNotifyingHost (v);
        repaint();
        return;
    }

    if (activeBand < 0)
        return;

    const auto pos = e.position;

    /* 按住耳机监听的相对扫频：先等鼠标离开按钮一段距离，再按
       "按住点 → 当前点"的偏移量移动频点（频率对数轴、增益线性轴），
       频点从原位置平滑跟手，不会吸附到鼠标按下处 */
    if (holdListen)
    {
        if (! holdDragActive)
        {
            if (pos.getDistanceFrom (holdPressPos) < 12.0f)
                return;
            holdDragActive = true;
        }

        const float dx = pos.x - holdPressPos.x;
        const float dy = pos.y - holdPressPos.y;

        if (dragFreq != nullptr)
        {
            const float f = sslClampFreq (processor, activeBand, juce::jlimit (Fusion::fMin, Fusion::fMax,
                holdStartFreq * std::exp (dx / (float) plotW * logRatio)));
            dragFreq->setValueNotifyingHost (
                dragFreq->getNormalisableRange().convertTo0to1 (f));
        }

        if (isCutBand (activeBand))
        {
            dragBandQFromY (activeBand, pos.y);   // 低切/高切：竖直 = 共振 Q
        }
        else if (dragGain != nullptr)
        {
            const float dbPerPx = (eqTop - eqBot) / (float) plotH;
            /* 屏幕向下 y 增大 → 衰减；向上 → 提升（与直接拖频点方向一致） */
            const float g = juce::jlimit (eqBot, eqTop, holdStartGain - dy * dbPerPx);
            dragGain->setValueNotifyingHost (
                dragGain->getNormalisableRange().convertTo0to1 (g));
        }

        repaint();
        return;
    }

    if (dragFreq != nullptr)
        dragFreq->setValueNotifyingHost (
            dragFreq->getNormalisableRange().convertTo0to1 (
                sslClampFreq (processor, activeBand, freqOfX (pos.x))));

    /* 低切 / 高切段没有增益维度：竖直拖动改共振 Q */
    if (isCutBand (activeBand))
    {
        dragBandQFromY (activeBand, pos.y);
    }
    else if (dragGain != nullptr)
    {
        dragGain->setValueNotifyingHost (
            dragGain->getNormalisableRange().convertTo0to1 (gainOfY (pos.y)));
    }

    repaint();
}

void SpectrumDisplay::mouseUp (const juce::MouseEvent&)
{
    /* BYPASS：拖拽状态已在 setBypassed 里收干净，这里不再重复处理 */
    if (bypassVisual)
        return;

    /* 松开泪滴尖端：结束 RANGE 手势 */
    if (rngTipDrag)
    {
        if (rngTipParam != nullptr)
            rngTipParam->endChangeGesture();

        rngTipDrag  = false;
        rngTipParam = nullptr;
        rngTipBand  = -1;
        repaint();
    }

    /* 松开压缩小旋钮：结束参数手势 */
    if (dynKnobDrag)
    {
        if (dynDragParam != nullptr)
            dynDragParam->endChangeGesture();

        dynKnobDrag  = false;
        dynDragParam = nullptr;
        dynKnobBand  = -1;
        repaint();
    }

    /* 松开浮窗耳机按钮：结束监听，然后照常结束拖动手势 */
    if (holdListen)
    {
        processor.setListenBand (-1);
        holdListen      = false;
        holdDragActive  = false;
        repaint();
    }

    endEdit();
    activeBand = -1;
    repaint();
}

void SpectrumDisplay::mouseMove (const juce::MouseEvent& e)
{
    if (bypassVisual)          // BYPASS：不悬停、不弹浮窗、不改鼠标指针
        return;

    /* 悬停判定收紧到"正压在频点上"（节点圆大小），
       靠近不再弹出悬浮框 —— 杜绝误触；
       拖拽抓取仍用 18px 宽容半径（mouseDown 里的 bandAt 默认值） */
    int hit = bandAt (e.position, 9.0f);
    const auto pos = e.position;
    const bool inside = (pos.x >= plotX && pos.x <= plotX + plotW
                      && pos.y >= plotY && pos.y <= plotY + plotH);

    /* 悬停在控制点上：抓手（可拖动）
       悬停在泪滴尖端：上下拖动光标（拖 = RANGE）
       悬停在悬浮窗内：普通箭头（避免误以为能拖节点）
       悬停在空白频谱上：十字（点一下就生成频点） */
    bool nearTip = false;

    if (processor.getMode() == Fusion::Mode::Clean)
        nearTip = dynZoneBandAt (pos) >= 0 && bandAt (pos, 8.0f) < 0;

    /* 浮窗显示期间记住段号：鼠标移进框内（按钮/旋钮操作）时保持显示 */
    if (const int curHud = hudBand(); curHud >= 0)
        hudPinBand = curHud;

    /* 淡出中的浮窗（0.5s 宽限）仍可操作：鼠标摸到框体就复活该段，
       否则按钮判定（hudBand()）已归 -1，点击会穿透到下面的频点 */
    if (! rngTipDrag && hudPinBand < 0 && hudAlpha > 0.5f && hudShowBand >= 0
        && hudRect (hudShowBand).expanded (8.0f).contains (pos))
        hudPinBand = hudShowBand;

    /* 框体四周留 8px 缓冲（与 timerCallback 的保持判定一致）：
       频点 → 浮窗之间的空隙不断链，鼠标能顺利进框操作 */
    const bool inHud = hudPinBand >= 0 && hudRect (hudPinBand).expanded (8.0f).contains (pos);
    const int cursorState = nearTip ? 3
                          : inHud  ? 2
                          : (hit >= 0 ? 0 : (inside ? 1 : 2));

    if (cursorState != cursorHint)
    {
        cursorHint = cursorState;
        setMouseCursor (cursorState == 0 ? juce::MouseCursor::DraggingHandCursor
                      : cursorState == 1 ? juce::MouseCursor::CrosshairCursor
                      : cursorState == 3 ? juce::MouseCursor::UpDownResizeCursor
                                         : juce::MouseCursor::NormalCursor);
    }

    if (inHud)
        hit = hudPinBand;
    else if (hit < 0)
        hudPinBand = -1;              // 离开框缓冲区且不在频点上：浮窗链断开，开始淡出

    if (hit != hoverBand)
    {
        hoverBand = hit;
        repaint();
    }
}

void SpectrumDisplay::mouseExit (const juce::MouseEvent&)
{
    hudPinBand = -1;

    if (hoverBand != -1 || cursorHint != 2)
    {
        hoverBand  = -1;
        cursorHint = -1;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void SpectrumDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (bypassVisual)          // BYPASS：双击开/关频段不生效
        return;

    int hit = bandAt (e.position);

    if (hit < 0)
        return;

    if (auto* on = processor.apvts.getParameter (IDs::bOn (hit)))
        on->setValueNotifyingHost (processor.getBandEnabled (hit) ? 0.0f : 1.0f);

    selectedBand = hit;
    repaint();
}

void SpectrumDisplay::mouseWheelMove (const juce::MouseEvent& e,
                                      const juce::MouseWheelDetails& wheel)
{
    if (bypassVisual)          // BYPASS：滚轮调 Q / RANGE 不生效
        return;

    const bool ctrl = e.mods.isCtrlDown() || e.mods.isCommandDown();

    /* 干净模式：滚轮悬停在作用区虚线（整条线）上 = 调该段 RANGE 宽度，
       和频点滚轮调 Q 同一套手感 */
    if (processor.getMode() == Fusion::Mode::Clean)
    {
        const int zb = dynZoneBandAt (e.position);

        if (zb >= 0)
        {
            if (auto* p = processor.apvts.getParameter (IDs::bDynRng (zb)))
            {
                const float cur = p->getNormalisableRange().convertFrom0to1 (p->getValue());
                const float nxt = juce::jlimit (0.0f, 24.0f, cur + wheel.deltaY * 4.0f);
                p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 (nxt));
            }

            selectedBand = zb;
            repaint();
            return;
        }
    }

    /* 监听中：滚轮始终作用于被监听的频段（边听边调宽度） */
    const int lbActive = processor.getListenBand();
    const int  hit  = (lbActive >= 0 ? lbActive
                     : hoverBand  >= 0 ? hoverBand
                     : (ctrl ? selectedBand : bandAt (e.position)));

    if (hit < 0)
        return;

    selectedBand = hit;

    const auto delta = wheel.deltaY;

    if (ctrl)
    {
        /* 微调增益：每次最多 0.5 dB */
        if (auto* p = gainParam (hit))
        {
            const auto cur = processor.getBandGain (hit);
            const auto nxt = juce::jlimit (-Fusion::gainRange, Fusion::gainRange,
                                           cur + delta * 12.0f);
            p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 (nxt));
        }
    }
    else if (isCutBand (hit))
    {
        /* 悬停在低切 / 高切频点上滚滚轮 = 逐档切斜率：
           6 → 12 → 18 → 24 → 30 → 36 → 48 → 72 → 96 → BRICKWALL(192 dB/oct)。
           上滚变陡；Shift = 一次跳 3 档，快速摸到砖墙。
           共振 Q 交给竖直拖动（见 mouseDrag），分工不冲突。 */
        if (auto* p = processor.apvts.getParameter (IDs::bSlope (hit)))
        {
            const auto& steps = Fusion::cutSlopeSteps();
            const int   jump  = e.mods.isShiftDown() ? 3 : 1;
            const int   idx   = juce::jlimit (0, Fusion::numCutSlopes - 1,
                                  Fusion::cutSlopeIndex (processor.getBandSlope (hit))
                                  + (delta > 0 ? jump : -jump));

            p->setValueNotifyingHost (
                p->getNormalisableRange().convertTo0to1 (steps[(size_t) idx]));
        }
    }
    else
    {
        /* 改 Q：按比例缩放，手感接近 Pro-Q */
        if (auto* p = processor.apvts.getParameter (IDs::bQ (hit)))
        {
            const auto cur = juce::jmax (0.05f, processor.getBandQ (hit));
            const auto nxt = juce::jlimit (Fusion::qMin, Fusion::qMax,
                                           cur * std::pow (1.6f, delta * 2.0f));
            p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 (nxt));
        }
    }

    repaint();
}
