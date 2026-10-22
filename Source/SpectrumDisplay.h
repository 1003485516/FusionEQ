/* =============================================================================
   Fusion EQ  ·  SpectrumDisplay.h
   实时频谱 + 频响曲线 + 可拖动的 6 个频段控制点（Pro-Q 式交互）

   交互（仅非 SSL 模式）：
     · 空白处按下        → 就在按下的位置生成一个控制点（Pro-Q 式直接塑形）
     · 拖动控制点        → 同时改频点（横轴）与增益/衰减（纵轴）
     · 滚轮（悬停在点）  → 改 Q；Ctrl + 滚轮 → 微调增益
     · 双击控制点        → 该段开 / 关
     · 鼠标停在机架某段  → 频谱里高亮该段的曲线与频点

   SSL 通透模式：setNodesVisible(false) → 纯观察视图，
   不画频点、不响应鼠标，全部调节走下方机架旋钮。
   ========================================================================== */
#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"   // 需要 CurveSnapshot / spectrumBins 完整类型

class SpectrumDisplay : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumDisplay (FusionEQAudioProcessor& p);
    ~SpectrumDisplay() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void setAccentColour (juce::Colour c);
    void setPeakHoldMode (bool h) noexcept { holdPeaks = h; }
    bool getPeakHoldMode() const noexcept  { return holdPeaks; }

    /* 鼠标停在机架某一段上时，突出显示该段的曲线与频点 */
    void setHighlightBand (int b) noexcept
    {
        if (highlightBand != b) { highlightBand = b; staticDirty = true; repaint(); }
    }

    /* SSL 模式：频点/曲线颜色换成下方旋钮的四色（LF黑 / LMF绿 / HMF蓝 / HF紫） */
    void setSslPalette (bool ssl)
    {
        if (sslPalette == ssl)
            return;
        sslPalette = ssl;
        staticDirty = true;
        repaint();
    }

    /* 频谱面板皮肤：跟随各模式机架的面板质感（不改机架本身）
       0 浅色（干净/默认） 1 SSL 灰蓝 2 Pultec 石墨 3 Neve 蓝灰 4 API 黑 5 Manley 黑 */
    void setPanelSkin (int s)
    {
        if (skin == s)
            return;
        skin = s;
        staticDirty = true;
        repaint();
    }

    /* BYPASS（面板按钮 或 宿主的 bypass 参数）：频谱整体变灰、禁止一切交互
       —— 频点/黄虚线/浮窗全部不动，避免 bypass 期间改了设置却没生效、
          退出 bypass 后频点串到别的位置。 */
    void setBypassed (bool b)
    {
        if (bypassVisual == b)
            return;

        bypassVisual = b;

        /* 中断进行中的一切交互，并把宿主手势收干净（否则 undo 状态卡住） */
        if (gestureActive)
            endEdit();

        if (dynDragParam != nullptr) { dynDragParam->endChangeGesture(); dynDragParam = nullptr; }
        if (rngTipParam  != nullptr) { rngTipParam->endChangeGesture();  rngTipParam  = nullptr; }

        activeBand = hoverBand = selectedBand = -1;
        hudPinBand = hudShowBand = -1;
        hudIdleMs = 0;
        hudAlpha = 0.0f;
        hudDynVis = 0.0f;
        holdListen = holdDragActive = false;
        dynKnobDrag = rngTipDrag = false;
        dynKnobBand = rngTipBand = -1;
        cursorHint = -1;
        processor.setListenBand (-1);

        /* SSL 观察模式本就不收鼠标；退出 bypass 时按当前模式还原 */
        setInterceptsMouseClicks (! b && showNodes, false);
        staticDirty = true;
        repaint();
    }

    bool isBypassShown() const noexcept { return bypassVisual; }

    /* SSL 通透模式：频谱只用来观察曲线走向 —— 不画频点、不响应鼠标，
       所有调节都走机架旋钮。其它模式恢复频点交互。 */
    void setNodesVisible (bool v)
    {
        if (showNodes != v)
        {
            showNodes = v;

            if (! v)
            {
                activeBand = hoverBand = selectedBand = -1;
                processor.setListenBand (-1);
            }
        }

        /* 兜底：无论状态有没有变都强制同步鼠标开关。
           BYPASS 优先级最高 —— 切模式时不能把 bypass 的禁用给解掉 */
        setInterceptsMouseClicks (! bypassVisual && showNodes, false);
        staticDirty = true;
        repaint();
    }

    /* --- 鼠标交互 --- */
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;

    /* 节点纵轴位置：低切/高切吸附到自身曲线（共振峰会随 Q 移动，
       固定画在增益参数上会看起来"错频"）；其它类型 = 增益参数位置 */
    float nodeYOf (int band) const;
    float bandCurveDbAt (int band, float freq) const;

    /* 低切 / 高切段：没有增益维度，竖直方向改为共振 Q。
       绘制与拖拽必须共用同一套换算，否则节点会与光标错位（跟手是硬要求）。
       cutQToDispDb / cutDispDbToQ 互为反函数：Q → 竖轴 dB ↔ 竖轴 dB → Q。 */
    static float cutQToDispDb (float q) noexcept;
    static float cutDispDbToQ (float db) noexcept;
    bool  isCutBand (int band) const;
    void  dragBandQFromY (int band, float yPos);

    FusionEQAudioProcessor& processor;

    juce::Colour accent { 0xff0e9bd6 };
    int  skin = 0;                 // 频谱面板皮肤（0 浅色；深色 = 各模式机架面板色）
    bool sslPalette = false;       // SSL：频点颜色跟随旋钮四色

    bool holdPeaks = false;
    bool showNodes = true;         // false = SSL 通透模式：纯观察视图
    bool bypassVisual = false;     // true = BYPASS：频谱变灰 + 禁用一切交互
    std::vector<float> peaks;

    static constexpr int padL = 38, padR = 24, padT = 12, padB = 24;
    static constexpr float specTop =   0.0f, specBot = -100.0f;   // 频谱 dBFS
    static constexpr float eqTop   =  18.0f, eqBot   =  -18.0f;   // EQ 增益 dB

    int plotX = 0, plotY = 0, plotW = 1, plotH = 1;

    int  activeBand = -1;      // 正在拖动
    int  hoverBand  = -1;
    int  selectedBand = -1;    // 上次操作过的段（用于左上角读数）
    int  highlightBand = -1;   // 鼠标停在机架上某一段 → 频谱里高亮它
    int  cursorHint = -1;      // 0=抓手 1=十字 2=普通（避免每帧设鼠标）

    juce::RangedAudioParameter* dragFreq = nullptr;
    juce::RangedAudioParameter* dragGain = nullptr;
    juce::RangedAudioParameter* dragQ    = nullptr;   // 低切/高切：竖直拖动改的是 Q
    bool gestureActive = false;
    bool holdListen    = false;     // 正在按住浮窗耳机按钮监听（松开即停）
    bool holdDragActive = false;    // 按住监听后已越过阈值、进入相对扫频
    juce::Point<float>  holdPressPos;    // 按下耳机时的鼠标位置
    float holdStartFreq = 0.0f;          // 按下时频点原始频率
    float holdStartGain = 0.0f;          // 按下时频点原始增益

    /* 悬浮框淡出：仅"选中"状态且鼠标 1 秒不在框内时淡出 */
    int   hudIdleMs = 0;
    float hudAlpha  = 1.0f;
    float hudDynVis = 0.0f;   // 干净模式：压缩旋钮区淡入淡出（0 隐藏 ~ 1 展开）

    /* 静态层缓存：背景/网格 + 曲线只在参数变化时重画一次，
       每帧只重画频谱与节点 —— 这是 60 fps 还能保持低占用的关键。 */
    juce::Image  bgLayer, curveLayer;
    bool         staticDirty = true;
    juce::uint64 lastStateHash = 0;

    /* 每帧从处理器 seqlock 拷贝一份快照到本地：
       绘制期间音频线程随时可能发布新快照，直接引用共享缓冲会读到
       新旧混合的撕裂帧（拖动频点时曲线"扯一下"）。 */
    FusionEQAudioProcessor::CurveSnapshot curveLocal {};
    std::array<float, FusionEQAudioProcessor::spectrumBins> specLocal {};
    bool snapshotsValid = false;
    juce::uint64 lastSnapVersion = 0;   // 感知音频线程重算过的快照（切模式等）

    void rebuildStaticLayer();
    juce::uint64 computeStateHash() const;

    /* 坐标变换 */
    float xOfFreq (float f) const noexcept;
    float freqOfX (float x) const noexcept;
    float yOfEq   (float db) const noexcept;
    float gainOfY (float y) const noexcept;
    float yOfSpec (float db) const noexcept;

    /* 命中测试：返回半径内最近的段，否则 -1
       radius 两用：拖拽抓取 18px（宽容），悬停触发悬浮框 9px（精准） */
    int bandAt (juce::Point<float> pos, float radius = 18.0f) const;

    /* --- Pro-Q 式信息浮窗：只在鼠标正压频点 / 拖动 / 监听 / 框内操作时显示
       （选中状态不再唤出浮窗 —— 杜绝曲线附近误触） --- */
    int hudBand() const;                                    // 当前浮窗跟随的频段
    int hudPinBand = -1;                                    // 浮窗正在显示时记住的段（供框内操作保持）
    int hudShowBand = -1;                                   // 淡出期间仍要绘制的段（alpha>0 期间保持）
    mutable int hudBelowBand = -1;                          // 浮窗当前落在频点下方的段（滞回防上下翻转闪烁）
    juce::Rectangle<float> hudRect (int band) const;        // 浮窗整体
    juce::Rectangle<float> hudHeadRect (int band) const;    // 耳机开关按钮（浮窗唯一控件）
    juce::Rectangle<float> hudDynRect (int band) const;     // DYN 开关（干净模式）
    juce::Rectangle<float> hudKnobRect (int band, int k) const; // 压缩小旋钮 0THR/1RNG/2ATT/3REL
    void drawHUD (juce::Graphics&);

    /* 悬浮框压缩小旋钮的垂直拖动状态 */
    bool  dynKnobDrag    = false;
    int   dynKnobBand    = -1;
    int   dynKnobIdx     = -1;
    float dynKnobStartY  = 0.0f;
    float dynKnobStartVal = 0.0f;
    juce::RangedAudioParameter* dynDragParam = nullptr;

    /* 压缩泪滴尖端拖动（垂直拖 = RANGE，Pro-Q 式） */
    bool  rngTipDrag  = false;
    int   rngTipBand  = -1;
    juce::RangedAudioParameter* rngTipParam = nullptr;
    juce::Point<float> dynTipPos (int b) const;   // 作用区尖端（屏幕坐标）
    int dynZoneBandAt (juce::Point<float> pos) const; // 距虚线 12px 内的段（整条线可抓）

    /* 参数读写 */
    juce::RangedAudioParameter* freqParam (int i);
    juce::RangedAudioParameter* gainParam (int i);
    double getParamReal (int band, int which) const;   // 0=freq 1=gain 2=q 3=on

    void beginEdit (int band);
    void endEdit();

    juce::Colour bandColour (int i) const;

    /* 绘制分块 */
    void drawGrid (juce::Graphics&, juce::Rectangle<int> area);
    void drawSpectrum (juce::Graphics&, juce::Rectangle<int> area);
    void drawCurves (juce::Graphics&, juce::Rectangle<int> area);
    void drawDynCurves (juce::Graphics&, juce::Rectangle<int> area);  // 动态压缩可视化（每帧）
    void drawNodes (juce::Graphics&, juce::Rectangle<int> area);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumDisplay)
};
