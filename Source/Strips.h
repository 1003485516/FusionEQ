/* =============================================================================
   Fusion EQ  ·  Strips.h
   机架上的三类条带：低切 / 6 段 EQ / AIR
   ============================================================================= */
#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ParamIDs.h"
#include "FusionKnob.h"
#include "SpectrumDisplay.h"

namespace Fusion
{

/* Pultec 拨盘 0-10 整数显示 */
inline juce::String formatPultec (float v) { return juce::String (juce::roundToInt (v)); }

/* BANDWIDTH 线性带宽 0-1：显示 SHARP↔BROAD 百分比 */
inline juce::String formatBand (float v)
{
    return juce::String (juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * 100.0f)) + " %";
}

/* ---------------------------------------------------------------------------
   ToggleLever —— Pultec 拨杆式档位选择控件
   贴图（6 = 长方拨杆 / 7 = 指针拨杆）绕底座圆心旋转到档位角度；
   点击循环下一档，滚轮 ±1；档位数值标注在柄尖外侧（指针指示）。
--------------------------------------------------------------------------- */
class ToggleLever : public juce::Component, private juce::Timer
{
public:
    ToggleLever (FusionEQAudioProcessor& p, const juce::String& paramId,
                 int texIndex, int numSteps, float travelDeg,
                 juce::Point<float> pivotUV, const juce::StringArray& stepNames)
        : proc (p), paramID (paramId), tex (texIndex), steps (numSteps),
          travel (travelDeg * juce::MathConstants<float>::pi / 180.0f),
          pivot (pivotUV), names (stepNames)
    {
        syncFromParam();
        startTimerHz (8);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& img = KnobLookAndFeel::sslKnobTex (tex);
        const float iw  = (float) img.getWidth();
        const float ih  = (float) img.getHeight();

        /* 布局：底座贴到组件底部再上抬 lift（往指针方向挪），柄朝上；旋转范围 ±travel */
        const float s        = leverSide / iw;
        const float pivotTx  = getWidth()  * 0.5f;
        const float pivotTy  = getHeight() - (1.0f - pivot.y) * leverSide - 2.0f - lift;
        const float t = steps > 1 ? (float) step / (float) (steps - 1) : 0.5f;
        const float a   = -travel + t * 2.0f * travel;

        /* 自建双三次重采样（带角度缓存），替代 JUCE 的双线性旋转 */
        KnobLookAndFeel::drawPivotedHQ (g, img, tex, getWidth(), getHeight(),
                                        pivotTx, pivotTy,
                                        pivot.x * iw, pivot.y * ih,
                                        iw / leverSide, a);

        /* 档位刻度：柄尖外侧，随档位角度弧形排布（当前档高亮） */
        const float labelR = pivot.y * ih * s + 15.0f;

        g.setFont (juce::Font (10.5f, juce::Font::bold));
        for (int i = 0; i < steps; ++i)
        {
            const float tt = steps > 1 ? (float) i / (float) (steps - 1) : 0.5f;
            const float ai = -travel + tt * 2.0f * travel;
            const float lx = pivotTx + labelR * std::sin (ai);
            const float ly = pivotTy - labelR * std::cos (ai);

            /* 硬件面板（SSL / Pultec 深色模块）→ 丝印浅字；浅色卡片 → 深色字 */
            g.setColour (i == step
                             ? (darkPanel ? juce::Colour (0xfff2f6fa) : juce::Colour (0xff181d23))
                             : (darkPanel ? juce::Colour (0xff8f989f) : juce::Colour (0xff79828c)));
            g.drawText (names[i],
                        juce::Rectangle<float> (lx, ly, 0.0f, 0.0f)
                            .withSizeKeepingCentre (64.0f, 14.0f),
                        juce::Justification::centred);
        }

        /* 底部铭牌（原版面板同款：LOW FREQUENCY / HIGH FREQUENCY / ATTEN SEL） */
        if (caption.isNotEmpty())
        {
            auto capFont = juce::Font (9.5f, juce::Font::bold).withExtraKerningFactor (0.10f);
            g.setFont (capFont);
            g.setColour (darkPanel ? juce::Colour (0xffaeb7be) : juce::Colour (0xff6b747e));
            g.drawText (caption,
                        juce::Rectangle<float> (0.0f, (float) getHeight() - 16.0f,
                                                (float) getWidth(), 14.0f),
                        juce::Justification::centred);
        }
    }

    void resized() override
    {
        leverSide = (float) juce::jmin (getWidth(), getHeight()) * sideScale;
    }

    /* 长拨杆（CPS/KCS）可调小，避免盖住刻度与相邻控件 */
    void setSideScale (float f) { sideScale = f; resized(); repaint(); }

    /* 底部铭牌文字（原版面板同款） */
    void setCaption (const juce::String& s) { caption = s; repaint(); }

    /* 硬件面板：刻度与铭牌切换成丝印浅色 */
    void setDarkPanel (bool d)
    {
        if (darkPanel == d)
            return;

        darkPanel = d;
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown())
            return;

        setStep ((step + 1) % steps);
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        const int d = wheel.deltaY > 0 ? -1 : 1;
        const int s = juce::jlimit (0, steps - 1, step + d);
        if (s != step)
            setStep (s);
    }

private:
    void timerCallback() override
    {
        const int cur = readStep();
        if (cur != step)
        {
            step = cur;
            repaint();
        }
    }

    int readStep() const
    {
        return juce::jlimit (0, steps - 1,
            juce::roundToInt (proc.apvts.getRawParameterValue (paramID)->load()));
    }

    void setStep (int s)
    {
        step = s;

        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (
                proc.apvts.getParameter (paramID)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) s));
            p->endChangeGesture();
        }
        repaint();
    }

    void syncFromParam()
    {
        step = readStep();
    }

    FusionEQAudioProcessor& proc;
    juce::String paramID;
    int   tex, steps;
    float travel;
    juce::Point<float> pivot;
    juce::StringArray names;

    int step = 0;
    float leverSide = 120.0f;
    float sideScale = 0.92f;   // 默认（ATTEN SEL 指针拨杆），长拨杆单独调小
    float lift      = 16.0f;   // 整体上抬（指针朝上 → 往指针方向挪）
    juce::String caption;      // 底部铭牌
    bool darkPanel = false;    // 深色硬件面板 → 刻度/铭牌用丝印浅字

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToggleLever)
};

/* ---------------------------------------------------------------------------
   带指示灯的 ON/OFF 开关：ON 亮灯（带辉光），OFF 灭灯
--------------------------------------------------------------------------- */
class LedButton : public juce::TextButton
{
public:
    LedButton() { setClickingTogglesState (true); }
    explicit LedButton (const juce::String& txt) : juce::TextButton (txt)
    {
        setClickingTogglesState (true);
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const bool on  = getToggleState();

        /* 硬件按钮皮肤：实拍两态素材直接铺满，悬停/按下轻微提亮 */
        if (hwSkin)
        {
            const auto& img = on ? hwOn : hwOff;
            g.drawImage (img, getLocalBounds().toFloat(),
                         juce::RectanglePlacement (juce::RectanglePlacement::centred), false);

            if (over || down)
            {
                g.setColour (juce::Colours::white.withAlpha (down ? 0.16f : 0.08f));
                g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 6.0f);
            }
            return;
        }

        juce::ignoreUnused (over);
        const bool lit = (on == ledWhenOn);   // 灯的极性：bypass 反接（处理中=亮）
        auto       r   = getLocalBounds().toFloat().reduced (1.0f);

        auto base = juce::Colour (lit ? 0xff2a313a : 0xff1f252b);
        if (down)      base = base.brighter (0.10f);
        else if (over) base = base.brighter (0.06f);

        g.setColour (base);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.30f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);

        /* 文字（略右移给指示灯让位）；BY PASS 等按钮保留自己的文本 */
        const juce::String txt = useOnOffText ? (on ? "ON" : "OFF") : getButtonText();
        g.setColour (lit ? juce::Colour (0xffe8eef4) : juce::Colour (0xff9aa4ae));
        g.setFont (juce::Font (juce::jmin (12.0f, getHeight() * 0.6f),
                               juce::Font::bold));
        g.drawText (txt, r.withTrimmedLeft (14.0f), juce::Justification::centred);

        /* 指示灯：亮 = 辉光，灭 = 暗灰 */
        auto led = juce::Rectangle<float> (r.getX() + 6.0f,
                                           r.getCentreY() - 3.5f, 7.0f, 7.0f);

        if (lit)
        {
            g.setColour (ledCol.withAlpha (0.28f));
            g.fillEllipse (led.expanded (4.5f));
            g.setColour (ledCol.brighter (0.35f));
        }
        else
        {
            g.setColour (juce::Colour (0xff39424b));
        }

        g.fillEllipse (led);
        g.setColour (juce::Colours::black.withAlpha (0.40f));
        g.drawEllipse (led, 0.8f);
    }

    void setLedColour (juce::Colour c) { ledCol = c; repaint(); }
    void setShowOnOffText (bool b)     { useOnOffText = b; repaint(); }
    void setLedPolarity (bool litWhenOn) { ledWhenOn = litWhenOn; repaint(); }
    bool isHardwareSkin() const        { return hwSkin; }

    /* 硬件按钮皮肤（厚实模式 HP 开关）：off/on 两态实拍素材，替换指示灯样式；
       传空图 = 恢复默认 LED 外观。点击/联动行为不变（仍走 ButtonAttachment）。 */
    void setHardwareSkin (const juce::Image& offImg, const juce::Image& onImg)
    {
        auto prep = [] (juce::Image src)
        {
            if (! src.isValid())
                return src;

            /* 渐进半分辨率到显示尺寸的 2 倍左右（显示约 68px 宽） */
            while (src.getWidth() > 160)
                src = src.rescaled (src.getWidth() / 2, src.getHeight() / 2,
                                    juce::Graphics::highResamplingQuality);
            return src;
        };

        hwOff = prep (offImg);
        hwOn  = prep (onImg);
        hwSkin = hwOff.isValid() && hwOn.isValid();
        repaint();
    }

private:
    juce::Colour ledCol { 0xff0e9bd6 };
    bool         useOnOffText { true };
    bool         ledWhenOn { true };   // false = 反接：toggle 关时亮灯（bypass 用）
    juce::Image  hwOff, hwOn;          // 硬件按钮两态素材（有效时启用皮肤）
    bool         hwSkin { false };
};

/* ---------------------------------------------------------------------------
   宝石旁通灯（温润模式 ATTEN 板块专属）：暗宝石 = BYPASSED，亮红宝石 = 处理中。
   与右上角 BYPASS 按钮双向联动（状态以 BYPASS 按钮为准）。
--------------------------------------------------------------------------- */
class GemButton : public juce::Button
{
public:
    GemButton() : juce::Button ("gem")
    {
        auto loadImg = [] (const char* d, int s)
        {
            juce::Image cur = juce::ImageFileFormat::loadFrom (d, (size_t) s);
            /* 渐进半分辨率（等效 mipmap）：显示尺寸约为 60px，先缩到 2 倍再画 */
            while (cur.getWidth() > 128)
                cur = cur.rescaled (cur.getWidth() / 2, cur.getHeight() / 2,
                                    juce::Graphics::highResamplingQuality);
            return cur;
        };
        imgOff = loadImg (BinaryData::gem_off_png, BinaryData::gem_off_pngSize);
        imgOn  = loadImg (BinaryData::gem_on_png,  BinaryData::gem_on_pngSize);
        setClickingTogglesState (true);
        setAlwaysOnTop (true);   // 悬在条带组件之上（条带后加入会盖住鼠标事件）
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        auto caption = r.removeFromBottom (15.0f);
        auto gem = r.reduced (2.0f);

        const auto& img = getToggleState() ? imgOff : imgOn;   // bypass 点亮 = 暗宝石

        g.drawImage (img, gem, juce::RectanglePlacement (juce::RectanglePlacement::centred), false);

        /* 悬停微亮，给可按手感 */
        if (over)
        {
            g.setColour (juce::Colours::white.withAlpha (down ? 0.16f : 0.08f));
            g.fillEllipse (gem);
        }

        g.setFont (juce::Font (9.5f, juce::Font::bold).withExtraKerningFactor (0.10f));
        g.setColour (juce::Colour (0xffaeb7be));
        g.drawText ("BYPASS", caption, juce::Justification::centred);
    }

private:
    juce::Image imgOff, imgOn;
};

/* ---------------------------------------------------------------------------
   6 段 EQ
--------------------------------------------------------------------------- */
/* SSL 4000 风格"∩"曲线小按钮：未按 = 原版搁架曲线（灰钮），按下 = 钟形（琥珀亮灯素材）。
   两态素材已统一比例（主体同宽、顶边对齐，画布 132×160）。 */
class CurveButton : public juce::Button
{
public:
    CurveButton() : juce::Button ("sslCurve") {}

    void setImages (const juce::Image& offImg, const juce::Image& onImg)
    {
        offI = offImg;
        onI  = onImg;
        repaint();
    }

    void setLit (bool shouldLit)
    {
        if (shouldLit == lit)
            return;
        lit = shouldLit;
        repaint();
    }

    void paintButton (juce::Graphics& g, bool /*over*/, bool down) override
    {
        const auto& img = lit ? onI : offI;
        if (! img.isValid())
            return;

        auto r = getLocalBounds().toFloat();
        if (down)
            r.translate (0.0f, 1.0f);

        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (img, r,
                     juce::RectanglePlacement (juce::RectanglePlacement::fillDestination
                                             | juce::RectanglePlacement::onlyReduceInSize),
                     false);
    }

private:
    juce::Image offI, onI;
    bool lit = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurveButton)
};


/* ---------------------------------------------------------------------------
   Manley Massive Passive 拨杆开关：垂直槽 + 金属拨杆。
   numStates = 3 → BOOST(上) / OUT(中) / CUT(下)；numStates = 2 → SHELF(上) / BELL(下)。
   点击循环切换；状态由外部同步（定时器轮询参数，支持宿主自动化回写）。
--------------------------------------------------------------------------- */
class MpToggle : public juce::Component
{
public:
    explicit MpToggle (int states) : numStates (juce::jlimit (2, 3, states)) {}

    int getState() const { return state; }

    void setState (int s, bool notify = true)
    {
        s = juce::jlimit (0, numStates - 1, s);

        if (s == state)
            return;

        state = s;
        repaint();

        if (notify && onClick)
            onClick();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const float w = r.getWidth(), h = r.getHeight();

        /* 杆座：垂直细槽（黑底 + 微内阴影） */
        auto slot = juce::Rectangle<float> (w * 0.30f, h * 0.08f, w * 0.40f, h * 0.84f);
        g.setColour (juce::Colour (0xff0d0e10));
        g.fillRoundedRectangle (slot, 3.0f);
        g.setColour (juce::Colour (0x59000000));
        g.drawRoundedRectangle (slot, 3.0f, 1.0f);

        /* 拨杆：银色金属块，位置 = 状态（0=上 1=中/下 2=下） */
        const float t     = state / (float) (numStates - 1);
        const float levH  = h * 0.32f;
        const float levY  = slot.getY() + (slot.getHeight() - levH) * t;
        auto lever = juce::Rectangle<float> (w * 0.24f, levY, w * 0.52f, levH);

        g.setColour (juce::Colour (0xffd3d8dd));
        g.fillRoundedRectangle (lever, 2.5f);
        g.setColour (juce::Colour (0x33ffffff));
        g.drawRoundedRectangle (lever.reduced (1.5f), 2.0f, 1.0f);
        g.setColour (juce::Colour (0x40000000));
        g.drawRoundedRectangle (lever, 2.5f, 1.0f);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        setState ((state + 1) % numStates);
    }

    std::function<void()> onClick;

private:
    int numStates = 2;
    int state = 1;             // 默认：OUT（三档）/ BELL（两档）

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MpToggle)
};


class BandStrip : public juce::Component, private juce::Timer
{
public:
    BandStrip (FusionEQAudioProcessor& p, int idx,
               const juce::String& nameEn, const juce::String& nameCn)
        : index (idx), proc (p)
    {
        title.setText (nameEn, juce::dontSendNotification);
        title.setJustificationType (juce::Justification::centred);
        title.setFont (juce::Font (11.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, juce::Colour (0xff5c666f));
        addAndMakeVisible (title);

        sub.setText (nameCn + U8 (" \u00b7 ") + juce::String (idx + 1), juce::dontSendNotification);
        sub.setJustificationType (juce::Justification::centred);
        sub.setFont (juce::Font (10.0f, juce::Font::bold));
        sub.setColour (juce::Label::textColourId, juce::Colour (0xff8b949c));
        addAndMakeVisible (sub);

        /* 左键点击 → 弹出 EQ 类型菜单（带 ▾ 下拉指示，对新手直观）。
           allowedTypes 非空时只列出允许的类型（SSL 4000E 设计：LF/LMF/HMF 固定 BELL） */
        typeButton.getProperties().set ("fontH", 11);
        typeButton.onClick = [this]()
        {
            if (menuLocked)                // 锁定类型：纯铭牌，无菜单
                return;

            const auto cur = juce::jlimit (0, (int) Fusion::BandType::Count - 1,
                juce::roundToInt (proc.apvts.getRawParameterValue (IDs::bType (index))->load()));

            juce::PopupMenu m;
            m.addSectionHeader ("EQ TYPE");

            auto addItem = [this, &m, &cur] (int t)
            {
                m.addItem (t + 1, TYPE_LABELS()[(size_t) t], true, t == cur,
                           Fusion::makeTypeIcon (t, juce::Colour (0xffe8eef4)));
            };

            if (allowedTypes.isEmpty())
                for (int t = 0; t < (int) Fusion::BandType::Count; ++t)
                    addItem (t);
            else
                for (auto t : allowedTypes)
                    addItem (t);

            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&typeButton),
                [this] (int result)
                {
                    if (result <= 0)
                        return;

                    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (
                            proc.apvts.getParameter (IDs::bType (index))))
                    {
                        p->beginChangeGesture();
                        p->setValueNotifyingHost (p->convertTo0to1 ((float) (result - 1)));
                        p->endChangeGesture();
                    }
                });
        };
        addAndMakeVisible (typeButton);

        /* SSL 4000 LF：∩ 曲线按钮（取代 Q 旋钮与类型菜单）。
           未按 = 原版搁架曲线；按下 = 钟形（亮灯素材）。Q 固定（旋钮隐藏，参数保持默认）。 */
        curveSymbol.setText (Fusion::U8 ("\u2229"), juce::dontSendNotification);
        curveSymbol.setJustificationType (juce::Justification::centred);
        curveSymbol.setFont (juce::Font (17.0f, juce::Font::bold));
        curveSymbol.setColour (juce::Label::textColourId, juce::Colour (0xff5c666f));
        addAndMakeVisible (curveSymbol);

        static const juce::Image curveOffImg = juce::ImageFileFormat::loadFrom (
            BinaryData::btn_ssl_curve_off_png, (size_t) BinaryData::btn_ssl_curve_off_pngSize);
        static const juce::Image curveOnImg  = juce::ImageFileFormat::loadFrom (
            BinaryData::btn_ssl_curve_on_png,  (size_t) BinaryData::btn_ssl_curve_on_pngSize);
        curveButton.setImages (curveOffImg, curveOnImg);
        curveButton.onClick = [this]()
        {
            const auto cur = juce::roundToInt (
                proc.apvts.getRawParameterValue (IDs::bType (index))->load());
            /* 弹起 = 该段原版搁架类型（LF=Low Shelf / HF=High Shelf），按下 = 钟形 */
            const int next = (cur == (int) Fusion::BandType::Bell)
                           ? curveShelfType
                           : (int) Fusion::BandType::Bell;
            if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (
                    proc.apvts.getParameter (IDs::bType (index))))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 ((float) next));
                p->endChangeGesture();
            }
        };
        curveButton.setVisible (false);
        curveSymbol.setVisible (false);
        addAndMakeVisible (curveButton);

        freqKnob = std::make_unique<FusionKnob> (proc.apvts, IDs::bFreq (index), "FREQ",
                                                Sweep {}, false, formatFreq);
        gainKnob = std::make_unique<FusionKnob> (proc.apvts, IDs::bGain (index), "GAIN",
                                                Sweep {}, true,  formatGain);
        qKnob    = std::make_unique<FusionKnob> (proc.apvts, IDs::bQ (index), "Q",
                                                Sweep {}, false, formatQ);

        addAndMakeVisible (*freqKnob);
        addAndMakeVisible (*gainKnob);
        addAndMakeVisible (*qKnob);

        onButton.setClickingTogglesState (true);
        onButton.setToggleState (true, juce::dontSendNotification);
        onButton.onClick = [this]()
        {
            const bool on = onButton.getToggleState();
            onButton.setButtonText (on ? "ON" : "OFF");
            if (auto* prm = dynamic_cast<juce::AudioParameterBool*> (
                    proc.apvts.getParameter (IDs::bOn (index))))
                prm->setValueNotifyingHost (on ? 1.0f : 0.0f);
        };
        addAndMakeVisible (onButton);

        /* 按当前参数值初始化类型按钮文字 */
        updateTypeLabel();

        /* 档位刻度高亮跟随参数（宿主自动化也能走） */
        startTimerHz (8);
    }

    /* 频点旋钮的子行程（SSL 原版各段频点范围）：只影响该模式下的旋钮行程，
       参数全局范围不变；越界的当前值会被钳进新范围。 */
    void setFreqRange (float loHz, float hiHz)
    {
        if (freqKnob == nullptr)
            return;

        if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (
                proc.apvts.getParameter (IDs::bFreq (index))))
        {
            const auto& nr = p->getNormalisableRange();
            freqKnob->setSubRange (nr.convertTo0to1 (loHz), nr.convertTo0to1 (hiHz));
        }
    }

    /* 类型菜单锁（仅 SSL 固定类型段）：只决定"能不能点 / 显示不显示 ▾"，
       与菜单内容（allowedTypes 过滤）彻底解耦 —— 避免白名单状态残留把菜单锁死 */
    void setTypeMenuLocked (bool locked)
    {
        const bool changed = (menuLocked != locked);
        menuLocked = locked;

        if (changed && ! locked)
            allowedTypes.clearQuick();       // 解锁 → 恢复完整菜单

        updateTypeLabel();
    }

    /* 模式限定的类型白名单：空 = 全部类型可选；单元素 = 固定（菜单只列该类型） */
    void setAllowedTypes (const juce::Array<int>& types)
    {
        /* 不做 early-return：即使白名单相同也要刷新文字/可点性 */
        allowedTypes = types;

        /* 锁定单一类型时，把参数钳到该值（旧工程可能存了别的类型） */
        if (menuLocked && allowedTypes.size() == 1)
        {
            const auto cur = juce::jlimit (0, (int) Fusion::BandType::Count - 1, juce::roundToInt (
                proc.apvts.getRawParameterValue (IDs::bType (index))->load()));
            if (cur != allowedTypes[0])
                if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (
                        proc.apvts.getParameter (IDs::bType (index))))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (p->convertTo0to1 ((float) allowedTypes[0]));
                    p->endChangeGesture();
                }
        }

        updateTypeLabel();
    }

    /* 类型按钮文字：锁定类型无 ▾（不是菜单，是铭牌） */
    void updateTypeLabel()
    {
        const auto t = juce::jlimit (0, (int) Fusion::BandType::Count - 1, juce::roundToInt (
            proc.apvts.getRawParameterValue (IDs::bType (index))->load()));

        typeButton.setButtonText (TYPE_LABELS()[(size_t) t] + (menuLocked ? juce::String() : U8 (" \u25be")));
        typeButton.setInterceptsMouseClicks (! menuLocked, false);
    }

    /* SSL 4000 LF 曲线按钮模式：隐藏 Q 旋钮与类型铭牌，显示 ∩ + 小方块按钮 */
    /* curl 按钮模式：-1 = 关闭（正常 Q 旋钮 + 类型菜单）；否则传入"弹起态"的搁架类型
       （LF = Low Shelf，HF = High Shelf）—— 按下 = 钟形，模拟 SSL 4000 硬件 BELL 键 */
    void setCurveMode (int shelfType)
    {
        const bool cm = shelfType >= 0;

        if (cm)
            curveShelfType = shelfType;

        curveMode = cm;
        qKnob->setVisible (! cm);
        typeButton.setVisible (! cm);
        curveButton.setVisible (cm);
        curveSymbol.setVisible (cm);
        updateCurveState();
        resized();
    }

    /* 亮灯状态跟随类型参数：Bell = 按下（亮），LowShelf = 未按 */
    void updateCurveState()
    {
        if (! curveMode)
            return;

        const auto cur = juce::roundToInt (proc.apvts.getRawParameterValue (IDs::bType (index))->load());
        curveButton.setLit (cur == (int) Fusion::BandType::Bell);
    }

    /* 类型铭牌显隐（SSL 的 LMF/HMF 固定 BELL，铭牌纯冗余 → 隐藏） */
    void setTypeBadgeVisible (bool v)
    {
        if (typeButton.isVisible() == v)
            return;
        typeButton.setVisible (v);
        resized();
    }

    /* 内容整体垂直居中（硬朗 550L：标题保留，旋钮两排 + ON 像低切卡一样居中） */
    void setContentCentered (bool b)
    {
        if (contentCentered == b)
            return;
        contentCentered = b;
        resized();
    }

    /* API 550L 招牌蓝字：频点数值用 API 蓝（真机 "Frequencies are indicated
       in blue numbers"），增益读数保持白字。仅硬朗模式启用。 */
    void setApiBlueFreqNums (bool b)
    {
        if (apiBlue == b)
            return;
        apiBlue = b;
        repaint();
    }

    /* Manley Massive Passive：每段顶部一对拨杆 ——
       BOOST/OUT/CUT（OUT = 该段硬旁通；BOOST/CUT 切换时把增益参数符号摆正）
       + SHELF/BELL（写 bType 参数：低两段 LowShelf，高两段 HighShelf） */
    void setMpMode (bool on, bool highPair)
    {
        if (mpMode == on && (! on || mpHighPair == highPair))
            return;

        mpMode     = on;
        mpHighPair = highPair;
        mpIn.setVisible (on);
        mpShape.setVisible (on);
        /* MP 量程标题（22 - 1K 等）是面板主丝印 → 字号放大；其它模式恢复 11px */
        title.setFont (juce::Font (on ? 15.0f : 11.0f, juce::Font::bold));

        if (on && mpIn.onClick == nullptr)
        {
            addAndMakeVisible (mpIn);
            addAndMakeVisible (mpShape);
            mpIn.onClick    = [this] { applyMpIn (mpIn.getState()); };
            mpShape.onClick = [this] { applyMpShape(); };
        }

        resized();
        repaint();
    }

    /* SSL 模式：三排旋钮整体放大（平分卡片剩余高度） */
    void setBigRows (bool b)
    {
        if (b == bigRows)
            return;
        bigRows = b;
        resized();
    }

    /* SSL 模式隐藏 ON/OFF（原版无开关，常开） */
    void setSwitchVisible (bool v)
    {
        if (onButton.isVisible() == v)
            return;
        onButton.setVisible (v);
        resized();
    }

    /* SSL 硬件皮肤：深灰模块面板 → 丝印白字（标题/副题/∩/旋钮标注） */
    void setHardwareSkin (bool hw)
    {
        if (hwSkin == hw)
            return;

        hwSkin = hw;
        title.setColour (juce::Label::textColourId,
            hw ? juce::Colour (0xffe9edf1) : juce::Colour (0xff5c666f));
        sub.setColour (juce::Label::textColourId,
            hw ? juce::Colour (0xffa8b2bb) : juce::Colour (0xff8b949c));
        curveSymbol.setColour (juce::Label::textColourId,
            hw ? juce::Colour (0xffa8b2bb) : juce::Colour (0xff5c666f));
        freqKnob->setDarkPanel (hw);
        gainKnob->setDarkPanel (hw);
        qKnob->setDarkPanel (hw);
        repaint();
    }

    /* 进入 SSL 时该段复位为原版搁架曲线（按钮未按状态） */
    void resetTypeToShelf()
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (
                proc.apvts.getParameter (IDs::bType (index))))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) curveShelfType));
            p->endChangeGesture();
        }
    }

    void paint (juce::Graphics& g) override
    {
        /* Manley：拨杆丝印（BOOST/OUT/CUT + SHELF/BELL，深面板浅字） */
        if (mpMode && mpIn.isVisible())
        {
            const auto ib = mpIn.getBounds().toFloat();
            const auto sb = mpShape.getBounds().toFloat();
            const auto silk = juce::Colour (0xffdfe4e9);

            g.setColour (silk);
            g.setFont (juce::Font (8.5f, juce::Font::bold));
            g.drawText ("BOOST", ib.getX() - 36, ib.getY() - 1, 34, 10,
                        juce::Justification::centredRight);
            g.drawText ("OUT",   ib.getRight() + 3, ib.getCentreY() - 5, 26, 10,
                        juce::Justification::centredLeft);
            g.drawText ("CUT",   ib.getX() - 36, ib.getBottom() - 9, 34, 10,
                        juce::Justification::centredRight);
            g.drawText ("SHELF", sb.getRight() + 3, sb.getY() - 1, 34, 10,
                        juce::Justification::centredLeft);
            g.drawText ("BELL",  sb.getRight() + 3, sb.getBottom() - 9, 34, 10,
                        juce::Justification::centredLeft);
        }

        /* Neve 频点钮环绕档位刻度：随指针方位角排布（-135°→+135°），
           当前档用强调色高亮 —— 硬件模拟无需数值读数，档位值印在钮周 */
        if (snapFreqs.size() >= 2 && freqKnob != nullptr && freqKnob->isVisible())
        {
            const auto  kb = freqKnob->getBounds().toFloat();
            const float cx = kb.getCentreX();
            const float cy = kb.getCentreY();
            const int   n  = snapFreqs.size();
            const float tickR = kb.getHeight() * 0.5f + 4.0f;

            auto freqLabel = [] (float hz)
            {
                if (hz >= 1000.0f)
                {
                    const float k = hz / 1000.0f;
                    return (std::abs (k - std::round (k)) < 0.05f)
                               ? juce::String (juce::roundToInt (k)) + "k"
                               : juce::String (k, 2).trimCharactersAtEnd ("0") + "k";
                }
                return juce::String (juce::roundToInt (hz));
            };

            g.setFont (juce::Font (10.5f, juce::Font::bold));
            for (int i = 0; i < n; ++i)
            {
                const float a  = (-135.0f + 270.0f * (float) i / (float) (n - 1))
                                 * juce::MathConstants<float>::pi / 180.0f;
                const float lx = cx + tickR * std::sin (a);
                const float ly = cy - tickR * std::cos (a);

                auto box = juce::Rectangle<float> (36.0f, 13.0f).withCentre ({ lx, ly });
                box = box.constrainedWithin (getLocalBounds().toFloat());

                /* Neve 深蓝灰面板 → 非当前档转丝印浅色；API 黑面板 → 招牌蓝字；
                   浅色卡片保持中灰 */
                g.setColour (i == lastSnapStep ? accentColour
                             : (apiBlue ? juce::Colour (0xff7d9fd6)
                              : title.isVisible() ? juce::Colour (0xff79828c)
                                                  : juce::Colour (0xffc3ccd3)));
                g.drawText (freqLabel (snapFreqs.getUnchecked (i)),
                            box, juce::Justification::centred);
            }
        }
    }

    /* 模式强调色 → 三个旋钮环 + ON 灯 + 频点刻度高亮（refreshMode 调用） */
    void setAccentColour (juce::Colour c)
    {
        accentColour = c;
        if (freqKnob != nullptr) freqKnob->setAccentColour (c);
        if (gainKnob != nullptr) gainKnob->setAccentColour (c);
        if (qKnob    != nullptr) qKnob->setAccentColour (c);
        onButton.setLedColour (c);
        repaint();
    }

    /* Manley 电子管皮肤开关（模式切换时由编辑器调用） */
    void setTubeStyle (bool shouldBeTube)
    {
        if (freqKnob != nullptr) freqKnob->setTubeStyle (shouldBeTube);
        if (gainKnob != nullptr) gainKnob->setTubeStyle (shouldBeTube);
        if (qKnob    != nullptr) qKnob->setTubeStyle    (shouldBeTube);
    }

    /* SSL 4000E 贴图皮肤：0=黑 1=绿 2=蓝 3=紫，-1 = 关闭 */
    void setTextureStyle (int texIndex)
    {
        if (freqKnob != nullptr) freqKnob->setTextureStyle (texIndex);
        if (gainKnob != nullptr) gainKnob->setTextureStyle (texIndex);
        if (qKnob    != nullptr) qKnob->setTextureStyle    (texIndex);
    }

    /* 分旋钮贴图（硬朗模式：FREQ=13 银 / GAIN=14 蓝 / Q=-1 矢量），-1 = 矢量银钮 */
    void setKnobTextures (int freqTex, int gainTex, int qTex)
    {
        if (freqKnob != nullptr) freqKnob->setTextureStyle (freqTex);
        if (gainKnob != nullptr) gainKnob->setTextureStyle (gainTex);
        if (qKnob    != nullptr) qKnob->setTextureStyle    (qTex);
    }

    /* 离散频点档（Neve 1073 式频点选择）：n 个 Hz 均分行程并吸附；
       空列表 = 恢复连续行程。档位数值由环绕刻度（paint）承担 */
    void setFreqSnap (std::initializer_list<float> freqs)
    {
        snapFreqs.clear();
        for (auto f : freqs)
            snapFreqs.add (f);
        lastSnapStep = -1;

        if (freqKnob != nullptr)
            freqKnob->setSnapHz (juce::Array<float> (freqs));

        repaint();
    }

    /* 频点钮自带名称/读数标签显隐（Neve：硬件模拟，档位值由环绕刻度指示，
       数值读数属冗余 → 隐藏；其它模式保持显示） */
    void setFreqLabelsVisible (bool shouldBeVisible)
    {
        if (freqKnob != nullptr)
            freqKnob->setLabelsVisible (shouldBeVisible);
    }

    /* 温暖 MP：GAIN 读数移到钮上方（下方位置被 FREQUENCY 环绕刻度占用） */
    void setGainValueOnTop (bool b)
    {
        if (gainKnob != nullptr)
            gainKnob->setValueOnTop (b);
    }

    /* 温暖 MP：GAIN 名称丝印隐藏（拨杆行已表意） */
    void setGainNameVisible (bool v)
    {
        if (gainKnob != nullptr)
            gainKnob->setNameVisible (v);
    }

    /* 厚实 1073：ON 开关换成 LOW/MID/HIGH 文字硬件按钮（off 暗 / on 亮）；
       传空图 = 恢复默认 LED 开关。点击/联动行为不变。 */
    void setOnHardwareSkin (const juce::Image& offImg, const juce::Image& onImg)
    {
        onButton.setHardwareSkin (offImg, onImg);
        resized();
        repaint();
    }

    /* 厚实 1073 深蓝灰面板：旋钮名称/读数标签转丝印浅色（复位区恢复） */
    void setKnobLabelsDark (bool d)
    {
        if (freqKnob != nullptr) freqKnob->setDarkPanel (d);
        if (gainKnob != nullptr) gainKnob->setDarkPanel (d);
        if (qKnob    != nullptr) qKnob->setDarkPanel    (d);
    }

    /* 频点旋钮显隐（Neve HIGH 固定 12 kHz，不需要频点钮） */
    void setFreqKnobVisible (bool shouldBeVisible)
    {
        if (freqKnob != nullptr && freqKnob->isVisible() != shouldBeVisible)
        {
            freqKnob->setVisible (shouldBeVisible);
            resized();
        }
    }

    /* 强制频点参数到固定值（Neve HIGH = 12 kHz，保证音频与硬件一致） */
    void forceFreqHz (float hz)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (
                proc.apvts.getParameter (IDs::bFreq (index))))
        {
            if (std::abs (p->get() - hz) > 0.01f)
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (hz));
                p->endChangeGesture();
            }
        }
    }

    /* Q 旋钮显隐（Neve 1073：Q 由硬件固定，不开放） */
    void setQVisible (bool shouldBeVisible)
    {
        if (qKnob != nullptr && qKnob->isVisible() != shouldBeVisible)
        {
            qKnob->setVisible (shouldBeVisible);
            resized();
        }
    }

    /* SSL 4 段模式下切换条带标题（LF / LMF / HMF / HF），离开 SSL 时还原 */
    void setBandTitles (const juce::String& nameEn, const juce::String& nameCn)
    {
        title.setText (nameEn, juce::dontSendNotification);
        sub.setText (nameCn, juce::dontSendNotification);
    }

    /* 条带标题（英文 + 中文两行）显隐：Neve 1073 硬件面板无此类丝印 → 隐藏；
       隐藏后顶部空间让给下方内容（与旋钮标签隐藏同一套处理） */
    void setTitlesVisible (bool shouldBeVisible)
    {
        title.setVisible (shouldBeVisible);
        sub.setVisible (shouldBeVisible);
        resized();
    }

    /* 鼠标进入 / 离开这一段 → 频谱里高亮对应的曲线与频点（-1 = 离开） */
    std::function<void (int)> onHover;

    void mouseEnter (const juce::MouseEvent&) override { if (onHover) onHover (index); }
    void mouseExit  (const juce::MouseEvent&) override { if (onHover) onHover (-1); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4, 8);

        if (title.isVisible())
        {
            title.setBounds (r.removeFromTop (mpMode ? 20 : 14));
            sub.setBounds   (r.removeFromTop (12));
            r.removeFromTop (2);
        }

        /* Manley Massive Passive：标题（频段量程）→ 拨杆行 → GAIN → FREQUENCY，
           整体在卡片内垂直居中；ON/Q/类型铭牌均不参与（由拨杆与固定 Q 承担） */
        if (mpMode)
        {
            constexpr int togH  = 44;
            constexpr int knobH = 66;      // GAIN：58 太小 → 回调到 66
            constexpr int freqH = 70;      // FREQUENCY
            constexpr int lift  = 14;      // 拨杆行 + GAIN 整体上移（频点不动）
            const int content = togH + 4 + knobH + freqH;
            auto body = r.withSizeKeepingCentre (r.getWidth(),
                                                 juce::jmin (content, r.getHeight()));

            auto upper = body.removeFromTop (togH + 4 + knobH);
            freqKnob->setBounds (body);                     // 频点钮：保持居中位置
            upper.translate (0, -lift);                     // 上部整体上提

            const auto tg = upper.removeFromTop (togH);
            mpIn   .setBounds (juce::Rectangle<int> (18, 36)
                                  .withCentre ({ tg.getCentreX() - 32, tg.getCentreY() }));
            mpShape.setBounds (juce::Rectangle<int> (18, 36)
                                  .withCentre ({ tg.getCentreX() + 32, tg.getCentreY() }));

            upper.removeFromTop (4);
            gainKnob->setBounds (upper);
            return;
        }

        if (typeButton.isVisible())
        {
            typeButton.setBounds (r.removeFromTop (17).withSizeKeepingCentre (78, 17));
            r.removeFromTop (5);
        }

        /* SSL：三排旋钮平分剩余高度（ON 开关隐藏后空间更充裕），比默认 88 明显更大 */
        const int reserve = onButton.isVisible() ? 23 : 0;
        int rowH = 88;

        if (bigRows)
            rowH = juce::jlimit (88, 106, (r.getHeight() - reserve) / 3);

        /* 标题隐藏（厚实 1073）或硬件居中形态（硬朗 550L）：条带按「两排旋钮 + ON」
           的同一基准垂直居中，保证各条带旋钮/开关水平对齐；
           其余模式（标题可见）保持原有顶部排布不变 */
        int gainRowH = rowH;

        if (! title.isVisible() || contentCentered)
        {
            /* 居中基准 = 整卡（与低切卡同一公式），标题区覆盖在上面即可 */
            const auto rC = (contentCentered && title.isVisible())
                                ? getLocalBounds().reduced (4, 8) : r;

            if (freqKnob == nullptr || ! freqKnob->isVisible())
            {
                gainRowH = rowH + 30;                       // 放大行高 → 旋钮盘面约等于频点钮
                r.removeFromTop (juce::jmax (0,
                    (rC.getHeight() - (gainRowH + reserve)) / 2));
            }
            else
                r.removeFromTop (juce::jmax (0,
                    (rC.getHeight() - (2 * rowH + reserve)) / 2));
        }

        if (freqKnob != nullptr && freqKnob->isVisible())   // 隐藏不占位
            freqKnob->setBounds (r.removeFromTop (rowH));
        if (gainKnob != nullptr)
            gainKnob->setBounds (r.removeFromTop (gainRowH));

        /* SSL LF/HF：曲线按钮占据 Q 的行位（curveMode 下 Q 钮隐藏但仍需布局） */
        if (curveMode || (qKnob != nullptr && qKnob->isVisible()))
        {
            auto qr = r.removeFromTop (juce::jmin (rowH, juce::jmax (0, r.getHeight())));

            if (curveMode)      // SSL LF/HF：Q 位置换成 曲线按钮 + ∩ 符号（竖排、与旋钮同轴）
            {
                constexpr int symH = 22;
                auto btnArea = qr.withTrimmedBottom (symH);
                const int bh = juce::jmin (btnArea.getHeight() / 2,
                                           juce::roundToInt (btnArea.getWidth() * 0.606f));
                const int bw = juce::roundToInt (bh * (132.0f / 160.0f));

                const auto btnBounds = juce::Rectangle<int> (bw, bh).withCentre (btnArea.getCentre());
                curveButton.setBounds (btnBounds);
                curveSymbol.setBounds (juce::Rectangle<int> (24, symH).withCentre (
                    juce::Point<int> (qr.getCentreX(), btnBounds.getBottom() + 6 + symH / 2)));
            }
            else
                qKnob->setBounds (qr);
        }

        if (onButton.isVisible())
        {
            if (onButton.isHardwareSkin())
            {
                /* 厚实 1073 / 硬朗 550L：硬件按钮贴卡片底部（68×28），
                   与低切 HP / AIR 按钮同一水平线 */
                const auto rF = getLocalBounds().reduced (4, 8);
                const int cy  = rF.getBottom() - 18;

                onButton.setBounds (juce::Rectangle<int> (68, 28)
                                        .withCentre ({ rF.getCentreX(), cy }));
            }
            else
            {
                r.removeFromTop (3);
                onButton.setBounds (r.removeFromTop (20).withSizeKeepingCentre (58, 20));
            }
        }
    }

    int index;
    LedButton       onButton   { "ON" };
    juce::TextButton typeButton { "BELL" };

private:
    FusionEQAudioProcessor& proc;
    juce::Label title, sub;
    std::unique_ptr<FusionKnob> freqKnob, gainKnob, qKnob;
    juce::Array<int> allowedTypes;      // 空 = 全部类型；单元素 = 固定
    CurveButton curveButton;            // SSL LF/HF：∩ 曲线按钮（原版搁架/钟形切换）
    juce::Label  curveSymbol;           // "∩" 曲线符号
    bool curveMode = false;
    int  curveShelfType = (int) Fusion::BandType::LowShelf;   // 弹起态类型（LF=Low / HF=High）
    bool bigRows = false;               // SSL：三排旋钮放大
    bool contentCentered = false;       // 硬朗 550L：内容垂直居中（标题保留）
    bool apiBlue = false;               // 硬朗 550L：频点数值 API 招牌蓝字
    bool menuLocked = false;            // SSL 固定类型段：菜单锁（可点性与白名单解耦）
    bool hwSkin = false;                // SSL 硬件皮肤：深面板 + 丝印白字
    bool mpMode = false;                // Manley Massive Passive：拨杆形态
    bool mpHighPair = false;            // 后两段 = HighShelf（前两段 = LowShelf）
    MpToggle mpIn { 3 };                // BOOST / OUT / CUT
    MpToggle mpShape { 2 };             // SHELF / BELL
    float lastOffGain = 0.0f;           // 旁通时上次见到的增益（检测“关着拧增益”）

    /* Neve 频点档位刻度：档位值（环绕标注）+ 当前档跟踪 */
    juce::Array<float> snapFreqs;
    int  lastSnapStep = -1;
    juce::Colour accentColour { 0xff181d23 };

    void timerCallback() override
    {
        const int cur = currentSnapStep();
        if (cur != lastSnapStep)
        {
            lastSnapStep = cur;
            repaint();
        }

        /* Massive Passive 拨杆状态同步（参数可能被宿主自动化改写） */
        if (mpMode)
        {
            const bool  on = proc.apvts.getRawParameterValue (IDs::bOn (index))->load() > 0.5f;
            const float gv = proc.apvts.getRawParameterValue (IDs::bGain (index))->load();

            /* 段旁通（OUT）时直接拧 GAIN：负增益自动落到 CUT、正增益自动落到
               BOOST —— 拧钮即刻生效，不用再先拨杆（宿主自动化同样适用） */
            if (! on && std::abs (gv) > 0.001f && gv != lastOffGain)
            {
                lastOffGain = gv;
                applyMpIn (gv < 0.0f ? 2 : 0);
            }
            else
            {
                lastOffGain = gv;
                mpIn.setState (! on ? 1 : (gv < -0.001f ? 2 : 0), false);
            }

            const int shelf = mpHighPair ? (int) Fusion::BandType::HighShelf
                                         : (int) Fusion::BandType::LowShelf;
            const int ty = (int) std::round (
                proc.apvts.getRawParameterValue (IDs::bType (index))->load());
            mpShape.setState (ty == shelf ? 0 : 1, false);
        }
    }

    /* 拨杆 → 参数：BOOST=开+正增益 / OUT=关 / CUT=开+负增益（符号由增益参数承担） */
    void applyMpIn (int st)
    {
        auto* onP = proc.apvts.getParameter (IDs::bOn (index));

        if (st == 1)                                  // OUT：该段硬旁通
        {
            if (onP != nullptr)
                onP->setValueNotifyingHost (0.0f);
            return;
        }

        if (onP != nullptr)
            onP->setValueNotifyingHost (1.0f);

        if (auto* gnP = dynamic_cast<juce::AudioParameterFloat*> (
                proc.apvts.getParameter (IDs::bGain (index))))
        {
            const float g = gnP->get();

            if ((st == 0 && g < -0.001f) || (st == 2 && g > 0.001f))
                gnP->setValueNotifyingHost (
                    gnP->getNormalisableRange().convertTo0to1 (-g));
        }
    }

    /* SHELF/BELL 拨杆 → bType 参数（Bell=0；低两段 LowShelf / 高两段 HighShelf） */
    void applyMpShape()
    {
        const int shelf = mpHighPair ? (int) Fusion::BandType::HighShelf
                                     : (int) Fusion::BandType::LowShelf;
        const int cur = (int) std::round (
            proc.apvts.getRawParameterValue (IDs::bType (index))->load());

        if (auto* p = proc.apvts.getParameter (IDs::bType (index)))
            p->setValueNotifyingHost (
                p->getNormalisableRange().convertTo0to1 (
                    (float) (cur == shelf ? (int) Fusion::BandType::Bell : shelf)));
    }

    int currentSnapStep() const
    {
        if (snapFreqs.size() < 2)
            return -1;

        if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (
                proc.apvts.getParameter (IDs::bFreq (index))))
        {
            const auto& nr  = p->getNormalisableRange();
            const double pn = nr.convertTo0to1 (p->get());   // 归一化 0..1

            int best = 0;
            double bd = 1.0e9;
            for (int k = 0; k < snapFreqs.size(); ++k)
            {
                const double d = std::abs (nr.convertTo0to1 (snapFreqs.getUnchecked (k)) - pn);
                if (d < bd) { bd = d; best = k; }
            }
            return best;
        }
        return -1;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandStrip)
};

/* ---------------------------------------------------------------------------
   低切：开关 + 斜率选择（12/24/36 dB/oct），频点不上明面（内部固定 80 Hz）
--------------------------------------------------------------------------- */
class HpfStrip : public juce::Component, private juce::Timer
{
public:
    explicit HpfStrip (juce::AudioProcessorValueTreeState& s)
        : apvts (&s),
          hpKnob (std::make_unique<FusionKnob> (s, IDs::filtHp, "HP",
                                                Sweep {}, false, filterFmt)),
          lpKnob (std::make_unique<FusionKnob> (s, IDs::filtLp, "LP",
                                                Sweep {}, false, filterFmt)),
          freqKnob (std::make_unique<FusionKnob> (s, IDs::hpfFreq, "HP",
                                                   Sweep {}, false,
                                                   [] (float idx)
                                                   {
                                                       static const char* steps[4] { "50 Hz", "80 Hz", "160 Hz", "300 Hz" };
                                                       return steps[juce::jlimit (0, 3, (int) std::round (idx))];
                                                   }))
    {
        addAndMakeVisible (onButton);
        addAndMakeVisible (slopeButton);

        /* 215L 侧链按钮（硬朗模式专用，纯面板装饰开关）：绿色指示灯 + SC 丝印 */
        scButton.setShowOnOffText (false);
        scButton.setLedColour (juce::Colour (0xff39d353));
        addAndMakeVisible (scButton);
        scButton.setVisible (false);

        /* SSL 原版滤波钮：红面瓷钮（tex10），默认隐藏。
           注意：先 addAndMakeVisible 再 setVisible(false) —— addAndMakeVisible
           内部会强制 setVisible(true)，顺序反了会让初始隐藏失效
           （首刷早退不再重设 → 步进钮在干净模式漏显的 bug 根因）。 */
        hpKnob->setTextureStyle (10);
        lpKnob->setTextureStyle (10);
        addAndMakeVisible (*hpKnob);
        addAndMakeVisible (*lpKnob);
        hpKnob->setVisible (false);
        lpKnob->setVisible (false);

        /* 低切频点步进钮（tex12 蓝钮，4 档 50/80/160/300 Hz），默认隐藏；
           读数由环绕刻度高亮承担（同参考硬件），关闭内置名称/读数标签 */
        freqKnob->setTextureStyle (12);
        freqKnob->setStepCount (4);
        freqKnob->setLabelsVisible (false);
        addAndMakeVisible (*freqKnob);
        freqKnob->setVisible (false);

        /* 斜率选择：点击弹出菜单（12/24/36 dB/oct，当前项打勾） */
        slopeButton.getProperties().set ("fontH", 12);
        slopeButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff20262c));
        slopeButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffc7d0d8));
        slopeButton.onClick = [this]()
        {
            const int cur = juce::jlimit (0, 2, juce::roundToInt (
                apvts->getRawParameterValue (IDs::hpfSlope)->load()));

            juce::PopupMenu m;
            m.addSectionHeader ("SLOPE");

            for (int s = 0; s < 3; ++s)
                m.addItem (s + 1, juce::String (12 * (s + 1)) + " dB/oct", true, s == cur);

            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slopeButton),
                [this] (int result)
                {
                    if (result <= 0)
                        return;

                    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (
                            apvts->getParameter (IDs::hpfSlope)))
                    {
                        p->beginChangeGesture();
                        p->setValueNotifyingHost (p->convertTo0to1 ((float) (result - 1)));
                        p->endChangeGesture();
                    }

                    updateSlopeText();
                });
        };

        /* 响应图跟着开关变色（按钮状态也可能被宿主自动化改动，所以轮询） */
        onButton.onClick = [this]() { repaint(); };
        lpButton.onClick = [this]() { repaint(); };
        lpButton.setVisible (false);
        addAndMakeVisible (lpButton);
        updateSlopeText();
        startTimerHz (8);
    }

    void timerCallback() override
    {
        const bool now = onButton.getToggleState();

        if (now != lastOnState)
        {
            lastOnState = now;
            repaint();
        }

        const int idx = juce::jlimit (0, 2, juce::roundToInt (
            apvts->getRawParameterValue (IDs::hpfSlope)->load()));

        if (idx != lastSlope)
        {
            lastSlope = idx;
            updateSlopeText();
        }

        /* 215L：两颗滤波钮档位变化 → 环绕刻度高亮跟着走 */
        const float hpHz = apvts->getRawParameterValue (IDs::filtHp)->load();
        const float lpHz = apvts->getRawParameterValue (IDs::filtLp)->load();

        if (std::abs (hpHz - lastHpHz) > 1.0e-3f || std::abs (lpHz - lastLpHz) > 1.0e-3f)
        {
            lastHpHz = hpHz;
            lastLpHz = lpHz;
            repaint();
        }

        /* 当前频点档位变化 → 刻度高亮跟着走 */
        const int fIdx = juce::jlimit (0, 3, juce::roundToInt (
            apvts->getRawParameterValue (IDs::hpfFreq)->load()));

        if (fIdx != lastFreq)
        {
            lastFreq = fIdx;
            repaint();
        }
    }

    /* 条带标题（LOW CUT / 低切·HPF 两行）显隐：Neve 1073 硬件面板无此类丝印 → 隐藏 */
    void setTitlesVisible (bool shouldBeVisible)
    {
        if (titlesVisible != shouldBeVisible)
        {
            titlesVisible = shouldBeVisible;
            resized();
            repaint();
        }
    }

    /* 厚实模式：ON/OFF 开关换成 1073 硬件 HP 按钮素材（off=暗 / on=亮），
       位置也移到与 EQ 条带 ON 同一水平线；其它模式恢复 LED 开关原样 */
    void setHwButtonSkin (bool useHw)
    {
        if (useHw)
        {
            onButton.setHardwareSkin (
                juce::ImageFileFormat::loadFrom (BinaryData::btn_hp_off_png,
                                                 (size_t) BinaryData::btn_hp_off_pngSize),
                juce::ImageFileFormat::loadFrom (BinaryData::btn_hp_on_png,
                                                 (size_t) BinaryData::btn_hp_on_pngSize));
        }
        else
        {
            onButton.setHardwareSkin ({}, {});
        }

        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (4, 8).toFloat();

        /* 硬朗 215L / 温暖 MP：环绕档位刻度（指针方位角 -135°~+135°）+ 丝印铭牌 */
        if (api215 && ! hpBlock.isEmpty())
        {
            struct Step { float hz; const char* txt; };

            /* Manley：LOW PASS 6K/7K5/9K/12K/18K/OFF；HIGH PASS OFF/22/39/68/120/220 */
            static const Step hpStepsMp[6] = { { 20.0f, "OFF" }, { 22.0f, "22" },
                                               { 39.0f, "39" }, { 68.0f, "68" },
                                               { 120.0f, "120" }, { 220.0f, "220" } };
            static const Step lpStepsMp[6] = { { 6000.0f, "6K" }, { 7500.0f, "7K5" },
                                               { 9000.0f, "9K" }, { 12000.0f, "12K" },
                                               { 18000.0f, "18K" }, { 20000.0f, "OFF" } };
            static const Step hpSteps[6] = { { 20.0f, "20" }, { 30.0f, "30" },
                                             { 50.0f, "50" }, { 100.0f, "100" },
                                             { 250.0f, "250" }, { 600.0f, "600" } };
            static const Step lpSteps[7] = { { 500.0f, "500" }, { 1000.0f, "1K" },
                                             { 2500.0f, "2.5K" }, { 5000.0f, "5K" },
                                             { 7500.0f, "7.5K" }, { 10000.0f, "10K" },
                                             { 20000.0f, "20K" } };

            const Step* hp = mpFilt ? hpStepsMp : hpSteps;
            const Step* lp = mpFilt ? lpStepsMp : lpSteps;
            const int   nHp = 6;
            const int   nLp = mpFilt ? 6 : 7;

            const float hpHz = apvts->getRawParameterValue (IDs::filtHp)->load();
            const float lpHz = apvts->getRawParameterValue (IDs::filtLp)->load();

            auto drawRing = [&] (juce::Rectangle<int> area, const Step* st, int n,
                                 float curHz)
            {
                const float cx = area.getCentreX(), cy = area.getCentreY();
                const float rad = 46.0f;
                const auto box = juce::Rectangle<float> (36, 12);

                for (int i = 0; i < n; ++i)
                {
                    const float ang = (-135.0f + 270.0f * i / (n - 1))
                                        * juce::MathConstants<float>::pi / 180.0f;
                    const float tx = cx + rad * std::sin (ang);
                    const float ty = cy - rad * std::cos (ang);

            g.setColour (std::abs (st[i].hz - curHz) < 1.0e-3f
                             ? accent
                             : (mpFilt ? juce::Colour (0xffdfe4e9)
                              : api215 ? juce::Colour (0xff7d9fd6)
                                                : juce::Colour (0xffc2c9c4)));
                    g.setFont (juce::Font (11.0f, juce::Font::bold));
                    g.drawText (st[i].txt, box.withCentre ({ tx, ty }),
                                juce::Justification::centred);
                }
            };

            drawRing (lpBlock, lp, nLp, lpHz);         // 上钮 = LOW PASS（filtLp）
            drawRing (hpBlock, hp, nHp, hpHz);         // 下钮 = HIGH PASS（filtHp）

            g.setColour (juce::Colour (0xffdfe6ec));   // 深面板丝印浅色
            g.setFont (juce::Font (11.0f, juce::Font::bold));
            g.drawText (mpFilt ? "LOW PASS" : "LO-PASS",
                        juce::Rectangle<float> (0, (float) hpBlock.getBottom(),
                                                (float) getWidth(), 13.0f),
                        juce::Justification::centred);
            g.drawText (mpFilt ? "HIGH PASS" : "HI-PASS",
                        juce::Rectangle<float> (0, (float) lpBlock.getBottom(),
                                                (float) getWidth(), 13.0f),
                        juce::Justification::centred);
            return;
        }

        if (sslFilt)       // SSL：原版 FILTER 卡片（只有标题，下面是两颗红钮）
        {
            g.setColour (juce::Colour (0xffe9edf1));      // 硬件面板丝印白
            g.setFont (juce::Font (11.0f, juce::Font::bold));
            g.drawText ("FILTER", r.removeFromTop (14.0f), juce::Justification::centred);

            g.setColour (juce::Colour (0xffa8b2bb));
            g.setFont (juce::Font (10.0f, juce::Font::bold));
            g.drawText (U8 ("\u6ee4\u6ce2 \u00b7 HP/LP"), r.removeFromTop (12.0f), juce::Justification::centred);
            return;
        }

        if (titlesVisible)
        {
            g.setColour (juce::Colour (0xff5c666f));
            g.setFont (juce::Font (11.0f, juce::Font::bold));
            g.drawText ("LOW CUT", r.removeFromTop (14.0f), juce::Justification::centred);

            g.setColour (juce::Colour (0xff8b949c));
            g.setFont (juce::Font (10.0f, juce::Font::bold));
            g.drawText (U8 ("\u4f4e\u5207 \u00b7 HPF"), r.removeFromTop (12.0f), juce::Justification::centred);
        }

        /* 硬朗模式：步进旋钮四档刻度（参考经典 HP 选择钮布局，当前档高亮）；
           其它模式：深色小屏幕 + 实际滤波器响应曲线（斜率/频点实时反映） */
        if (freqKnob->isVisible() && ! knobArea.isEmpty())
        {
            const int  cx = knobArea.getCentreX();
            const int  cy = knobArea.getCentreY();
            const int cur = juce::jlimit (0, 3, juce::roundToInt (
                apvts->getRawParameterValue (IDs::hpfFreq)->load()));
            const auto hi = accent;
            /* Neve 深蓝灰面板（标题隐藏）→ 非当前档丝印浅色 */
            const auto lo = titlesVisible ? juce::Colour (0xff8b949c)
                                          : juce::Colour (0xffc3ccd3);

            /* 四档位置 = 指针实际指向：50→左下(-135°) / 80→左上(-45°)
               / 160→右上(+45°) / 300→右下(+135°)，标签跟着指针走 */
            struct Tick { const char* txt; juce::Rectangle<float> box; };
            const Tick ticks[4] {
                { "50",  { (float) (cx - 48), (float) (cy + 32), 32.0f, 12.0f } },
                { "80",  { (float) (cx - 48), (float) (cy - 44), 32.0f, 12.0f } },
                { "160", { (float) (cx + 16), (float) (cy - 44), 32.0f, 12.0f } },
                { "300", { (float) (cx + 16), (float) (cy + 32), 32.0f, 12.0f } },
            };

            for (int i = 0; i < 4; ++i)
            {
                g.setColour (i == cur ? hi : lo);
                g.setFont (juce::Font (11.0f, juce::Font::bold));
                g.drawText (ticks[i].txt, ticks[i].box, juce::Justification::centred);
            }

            /* 当前档位数值（旋钮下方大字，accent 高亮） */
            static const char* vals[4] { "50 Hz", "80 Hz", "160 Hz", "300 Hz" };
            if (! valueRow.isEmpty())
            {
                g.setColour (accent);
                g.setFont (juce::Font (13.0f, juce::Font::bold));
                g.drawText (vals[cur], valueRow.toFloat(), juce::Justification::centred);
            }
        }
        else if (! sslFilt && ! screenArea.isEmpty())
        {
            auto sr = screenArea.toFloat();
            g.setColour (juce::Colour (0xff14181d));                  // 屏幕底
            g.fillRoundedRectangle (sr, 5.0f);
            g.setColour (juce::Colour (0xff2c333b));                  // 屏框
            g.drawRoundedRectangle (sr.reduced (0.5f), 5.0f, 1.0f);

            /* 实测响应：按当前斜率的 Butterworth 级联逐节计算 |H|（与音频链同参） */
            static constexpr double secQ[3][3] {
                { 0.7071,  0.0,    0.0    },   // 12 dB/oct
                { 0.5412,  1.3066, 0.0    },   // 24 dB/oct
                { 0.5176,  0.7071, 1.9319 }    // 36 dB/oct
            };
            const int stages = 1 + juce::jlimit (0, 2, juce::roundToInt (
                apvts->getRawParameterValue (IDs::hpfSlope)->load()));
            const int fIdx = juce::jlimit (0, 3, juce::roundToInt (
                apvts->getRawParameterValue (IDs::hpfFreq)->load()));
            static constexpr float fSteps[4] { 50.0f, 80.0f, 160.0f, 300.0f };
            const double f0 = fSteps[fIdx];

            juce::Path p;
            for (int i = 0; i <= 60; ++i)
            {
                const double f  = 20.0 * std::pow (1000.0, i / 60.0);   // 20 Hz → 20 kHz 对数
                double       db = 0.0;
                for (int s = 0; s < stages; ++s)
                {
                    const double q = secQ[stages - 1][s];
                    const double w = f / f0;
                    db += juce::Decibels::gainToDecibels (
                        (w * w) / std::sqrt ((1.0 - w * w) * (1.0 - w * w) + (w / q) * (w / q)));
                }
                const double y = juce::jmap (juce::jlimit (-24.0, 6.0, db), -24.0, 6.0,
                                             (double) sr.getBottom() - 3.0, (double) sr.getY() + 3.0);
                const double x = juce::jmap (std::log (f / 20.0) / std::log (1000.0), 0.0, 1.0,
                                             (double) sr.getX() + 2.0, (double) sr.getRight() - 2.0);
                if (i == 0) p.startNewSubPath ((float) x, (float) y);
                else        p.lineTo ((float) x, (float) y);
            }
            g.setColour (onButton.getToggleState() ? accent.brighter (0.25f)
                                                   : juce::Colour (0xff9aa4ad));
            g.strokePath (p, juce::PathStrokeType (1.6f));
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4, 8);
        if (titlesVisible)
        {
            r.removeFromTop (13);                              // 标题（绘制）
            r.removeFromTop (11);                              // 副标题（绘制）
        }

        if (api215)        // 硬朗 215L：LO-PASS / HI-PASS 双钮 + HP/LP 开关，卡片内垂直居中
        {
            constexpr int blockH = 112;    // 单个滤波块（旋钮 + 环绕刻度空间）
            constexpr int labelH = 13;     // LO-PASS / HI-PASS 丝印
            constexpr int rowH   = 28;     // HP / LP 开关行（贴底）
            const int content = blockH * 2 + labelH * 2 + 4;
            auto body = r.withSizeKeepingCentre (r.getWidth(),
                                                 juce::jmin (content, r.getHeight()));

            hpBlock = body.removeFromTop (blockH);
            hpKnob->setBounds (hpBlock.withSizeKeepingCentre (76, 76));
            body.removeFromTop (labelH);

            lpBlock = body.removeFromTop (blockH);
            lpKnob->setBounds (lpBlock.withSizeKeepingCentre (76, 76));

            /* 底部开关行：HP（高通，LO-PASS 段）+ LP（低通，HI-PASS 段），68×28 贴底 */
            if (onButton.isVisible() || lpButton.isVisible())
            {
                const auto rF = getLocalBounds().reduced (4, 8);
                const int  cy = rF.getBottom() - 18;

                onButton.setBounds (juce::Rectangle<int> (68, 28)
                                        .withCentre ({ rF.getCentreX() - 39, cy }));
                lpButton.setBounds (juce::Rectangle<int> (68, 28)
                                        .withCentre ({ rF.getCentreX() + 39, cy }));
            }
            return;
        }

        if (sslFilt)       // SSL：HP / LP 两颗红钮上下排布，在卡片内垂直居中
        {
            constexpr int knobH = 126;
            const int stackH = knobH * 2 + 6;
            auto body = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (stackH, r.getHeight()));

            hpKnob->setBounds (body.removeFromTop (knobH).withSizeKeepingCentre (100, knobH));
            body.removeFromTop (6);
            lpKnob->setBounds (body.removeFromTop (knobH).withSizeKeepingCentre (100, knobH));
            return;
        }

        /* 步进蓝钮（仅厚实模式）：88×88（与 EQ 频点钮同一模板同尺寸），
           下方一行大字数值（50/80/160/300 Hz），四档刻度环绕（paint 绘制）；
           标题隐藏（厚实）时内容块整体垂直居中；其它模式：响应曲线小屏幕 */
        if (freqKnob->isVisible())
        {
            const int knobH = 88;

            /* 顶部留白与 BandStrip（频点+增益两排 + ON）同一公式：
               保证厚实模式下低切频点钮与 LOW/MID 频点钮水平对齐 */
            if (! titlesVisible)
                r.removeFromTop (juce::jmax (0, (r.getHeight() - (knobH * 2 + 23)) / 2));

            knobArea = r.removeFromTop (knobH);
            freqKnob->setBounds (knobArea.withSizeKeepingCentre (knobH, knobH));
            valueRow = r.removeFromTop (16);
            r.removeFromTop (4);
        }
        else
        {
            /* 响应曲线小屏幕 + 斜率 + OFF 整组在卡片剩余空间内垂直居中 */
            constexpr int stackH = 64 + 8 + 22 + 4 + 22;
            r.removeFromTop (juce::jmax (0, (r.getHeight() - stackH) / 2));

            screenArea = r.removeFromTop (64);
            r.removeFromTop (8);
        }
        slopeButton.setBounds (r.removeFromTop (22).withSizeKeepingCentre (100, 22));

        if (! titlesVisible)
        {
            /* 硬件 HP 开关：贴卡片底部（与 EQ 条带 / AIR 硬件按钮同一水平线） */
            const auto  rFull = getLocalBounds().reduced (4, 8);
            const int   cy    = rFull.getBottom() - 18;

            onButton.setBounds (juce::Rectangle<int> (68, 28)
                                    .withCentre ({ rFull.getCentreX(), cy }));
        }
        else
        {
            r.removeFromTop (4);
            onButton.setBounds (r.removeFromTop (22).withSizeKeepingCentre (62, 22));
        }
    }

    void setAccent (juce::Colour c) { accent = c; onButton.setLedColour (c); repaint(); }

    /* Manley Massive Passive 高/低切：两颗黑色旋钮（LOW PASS 在上 / HIGH PASS 在下），
       OFF = 端点档（filtLp 最右 / filtHp 最左，与 SSL 旁通语义一致），
       无独立开关（原机 OFF 在旋钮档位上）。布局复用 215L 双钮分支。 */
    void setMpFilt (bool on)
    {
        if (mpFilt == on)
            return;

        mpFilt = on;

        hpKnob->setVisible (on || sslFilt);
        lpKnob->setVisible (on || sslFilt);

        if (on)
        {
            api215 = true;
            /* 原机旋钮是黑色电子管风格（同温暖模式六段钮 tex4） */
            hpKnob->setTextureStyle (4);
            lpKnob->setTextureStyle (4);
            hpKnob->setLabelsVisible (false);
            lpKnob->setLabelsVisible (false);
            /* HIGH PASS（filtHp）：OFF(20) / 22 / 39 / 68 / 120 / 220 Hz */
            hpKnob->setSnapHz ({ 20.0f, 22.0f, 39.0f, 68.0f, 120.0f, 220.0f });
            /* LOW PASS（filtLp）：6K / 7K5 / 9K / 12K / 18K / OFF(20K) */
            lpKnob->setSnapHz ({ 6000.0f, 7500.0f, 9000.0f, 12000.0f, 18000.0f, 20000.0f });
        }
        else
        {
            if (api215)
            {
                /* 回到 215L（温暖 → 硬朗）：恢复橙芯钮与 215L 档位 */
                hpKnob->setTextureStyle (17);
                lpKnob->setTextureStyle (17);
                hpKnob->setLabelsVisible (false);
                lpKnob->setLabelsVisible (false);
                hpKnob->setSnapHz ({ 20.0f, 30.0f, 50.0f, 100.0f, 250.0f, 600.0f });
                lpKnob->setSnapHz ({ 500.0f, 1000.0f, 2500.0f, 5000.0f,
                                     7500.0f, 10000.0f, 20000.0f });
                onButton.setVisible (! sslFilt);
                lpButton.setVisible (true);
                lpButton.setHardwareSkin (
                    juce::ImageFileFormat::loadFrom (BinaryData::btn_lp_off_png,
                                                     (size_t) BinaryData::btn_lp_off_pngSize),
                    juce::ImageFileFormat::loadFrom (BinaryData::btn_lp_on_png,
                                                     (size_t) BinaryData::btn_lp_on_pngSize));
            }
            else
            {
                hpKnob->setTextureStyle (10);
                lpKnob->setTextureStyle (10);
                hpKnob->setLabelsVisible (true);
                lpKnob->setLabelsVisible (true);
                hpKnob->setSnapHz ({});
                lpKnob->setSnapHz ({});
            }
        }

        slopeButton.setVisible (! on && ! sslFilt && ! api215);
        freqKnob->setVisible (! on && ! sslFilt && stepper);
        onButton.setVisible (! on && ! sslFilt);
        lpButton.setVisible (false);

        resized();
        repaint();
    }

    /* SSL 模式：LOW CUT 换成原版 4000 的 FILTER（HP/LP 两颗红钮，常开固定 12 dB/oct） */
    void setSslFilter (bool ssl)
    {
        if (ssl == sslFilt)
            return;

        sslFilt = ssl;
        hpKnob->setVisible (ssl);
        lpKnob->setVisible (ssl);
        hpKnob->setDarkPanel (ssl);    // 硬件面板 → 丝印白字读数
        lpKnob->setDarkPanel (ssl);
        freqKnob->setVisible (! ssl && stepper);  // 仅硬朗模式 + 非 SSL：步进蓝钮
        slopeButton.setVisible (! ssl);
        onButton.setVisible (! ssl);
        resized();
        repaint();
    }

    /* 低切频点步进蓝钮（50/80/160/300）：只在硬朗（API）模式显示，
       其它模式隐藏（频点固定 80 Hz），由编辑器在模式切换时调用 */
    void setStepperVisible (bool shouldBeVisible)
    {
        if (stepper == shouldBeVisible)
            return;

        stepper = shouldBeVisible;
        freqKnob->setVisible (stepper && ! sslFilt);
        resized();
        repaint();
    }

    /* 硬朗模式：低切卡换成 API 215L 双滤波（LO-PASS + HI-PASS 橙芯步进钮 +
       SC/ON 硬件开关）。滤波走 SSL 的 filtHp/filtLp 参数（复用 12 dB/oct 链），
       档位离散吸附；ON 仍绑定 hpfOn（编辑器里的 ButtonAttachment 不变） */
    void setApi215 (bool on)
    {
        if (api215 == on)
            return;

        api215 = on;
        hpKnob->setVisible (on || sslFilt);
        lpKnob->setVisible (on || sslFilt);
        hpKnob->setTextureStyle (on ? 17 : 10);
        lpKnob->setTextureStyle (on ? 17 : 10);
        /* 215L：档位值由环绕刻度指示（硬件模拟，不显示读数） */
        hpKnob->setLabelsVisible (! on);
        lpKnob->setLabelsVisible (! on);

        if (on)
        {
            hpKnob->setSnapHz ({ 20.0f, 30.0f, 50.0f, 100.0f, 250.0f, 600.0f });
            lpKnob->setSnapHz ({ 500.0f, 1000.0f, 2500.0f, 5000.0f,
                                 7500.0f, 10000.0f, 20000.0f });
        }
        else
        {
            hpKnob->setSnapHz ({});
            lpKnob->setSnapHz ({});
        }

        slopeButton.setVisible (! on && ! sslFilt);
        freqKnob->setVisible (! on && ! sslFilt && stepper);
        /* 215L：HP（高通）/ LP（低通）独立通断，两个硬件按钮都可按 */
        onButton.setVisible (! sslFilt);
        lpButton.setVisible (on);

        if (on)
        {
            lpButton.setHardwareSkin (
                juce::ImageFileFormat::loadFrom (BinaryData::btn_lp_off_png,
                                                 (size_t) BinaryData::btn_lp_off_pngSize),
                juce::ImageFileFormat::loadFrom (BinaryData::btn_lp_on_png,
                                                 (size_t) BinaryData::btn_lp_on_pngSize));
        }
        else
        {
            lpButton.setHardwareSkin ({}, {});
        }

        resized();
        repaint();
    }

    LedButton onButton { "ON" };
    LedButton lpButton { "LP" };                       // 215L：LO-PASS 独立开关（硬朗模式）
    LedButton scButton { "SC" };                       // 215L 面板装饰开关（硬朗模式）

private:
    /* 原版 4000：HP 拧到最左 / LP 拧到最右 = OUT（旁通），读数显示 OUT */
    static juce::String filterFmt (float f)
    {
        if (f <= 20.5f || f >= 19995.0f)
            return "OUT";
        return Fusion::formatFreq (f);
    }

    void updateSlopeText()
    {
        const int idx = juce::jlimit (0, 2, juce::roundToInt (
            apvts->getRawParameterValue (IDs::hpfSlope)->load()));
        slopeButton.setButtonText (juce::String (12 * (idx + 1)) + " dB/oct " + U8 ("\u25be"));
    }

    juce::AudioProcessorValueTreeState* apvts = nullptr;
    juce::TextButton slopeButton { "12 dB/oct" };
    std::unique_ptr<FusionKnob> hpKnob, lpKnob;        // SSL：HP/LP 红钮（tex10）
    std::unique_ptr<FusionKnob> freqKnob;              // 低切频点步进蓝钮（tex12，仅硬朗模式）
    juce::Rectangle<int> knobArea;                     // 步进钮区域（刻度环绕绘制）
    juce::Rectangle<int> valueRow;                     // 步进钮下方数值行
    juce::Rectangle<int> screenArea;                   // 响应曲线小屏幕（非硬朗模式）
    bool sslFilt = false;
    bool api215 = false;                               // 硬朗 215L 双滤波形态
    bool mpFilt = false;                               // Manley 高/低切（复用 215L 布局，OFF=端点）
    juce::Rectangle<int> hpBlock, lpBlock;             // 215L 两颗钮的刻度区域
    float lastHpHz = -1.0f, lastLpHz = -1.0f;
    bool titlesVisible = true;                         // 条带标题显隐（Neve 隐藏）
    bool stepper = false;                              // 步进钮是否随硬朗模式显示
    int  lastSlope = 0;
    int  lastFreq  = -1;
    juce::Colour accent { 0xff0e9bd6 };
    bool lastOnState = true;
};

/* ---------------------------------------------------------------------------
   AIR（11 kHz 平行 EQ）
--------------------------------------------------------------------------- */
class AirStrip : public juce::Component, private juce::Timer
{
public:
    explicit AirStrip (FusionEQAudioProcessor& p)
        : proc (p),
          airKnob (std::make_unique<FusionKnob> (p.apvts, IDs::airAmt, "Magic Air",
                                                 airSweep(), false, formatAir))
    {
        /* AIR 是插件招牌：银白机身 + 放大的字标，与其它炭灰旋钮形成对比 */
        airKnob->setSilverBody (true);
        airKnob->setLabelSize (14.0f, juce::Colour (0xff4a5560));
        addAndMakeVisible (*airKnob);
        addAndMakeVisible (onButton);

        /* AIR 关闭时锁定旋钮（8 Hz 轮询，兼顾宿主自动化） */
        startTimerHz (8);
    }

    void paint (juce::Graphics&) override { }

    void timerCallback() override
    {
        const bool on = proc.apvts.getRawParameterValue (IDs::airOn)->load() > 0.5f;

        if (on != lastOn)
        {
            lastOn = on;
            airKnob->setLocked (! on);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4, 8);
        r.removeFromTop (13);                            // 标题占位（不绘制）
        r.removeFromTop (11);

        /* 旋钮块 + ON 按钮作为一组，在卡片剩余空间内垂直居中 */
        constexpr int knobH = 126, gap = 8;
        const bool  hwBtn  = onButton.isHardwareSkin();
        const int   btnH   = hwBtn ? 28 : 22;
        const int   btnW   = hwBtn ? 68 : 62;
        /* 厚实：按钮移到底部公共行（不参与 stack 居中），旋钮块单独居中 */
        const int stackH = knobH + (hwBtn ? 0 : gap + btnH);
        auto body = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (stackH, r.getHeight()));

        if (airKnob != nullptr)    // 防御：构造早期可能被意外触发
            /* 贴图皮肤下用 118 宽容器，与其它模式旋钮同尺寸 */
            airKnob->setBounds (body.removeFromTop (knobH)
                                        .withSizeKeepingCentre (skinTex >= 0 ? 118 : 100, knobH));
        else
            body.removeFromTop (knobH);

        if (hwBtn)
        {
            /* 厚实 1073 / 硬朗 550L：硬件按钮贴卡片底部
               （与 EQ 条带 / 低切 HP 硬件按钮同一水平线） */
            const auto  rF  = getLocalBounds().reduced (4, 8);
            const int   cy  = rF.getBottom() - 18;

            onButton.setBounds (juce::Rectangle<int> (btnW, btnH)
                                    .withCentre ({ rF.getCentreX(), cy }));
        }
        else
        {
            body.removeFromTop (gap);
            onButton.setBounds (body.removeFromTop (btnH).withSizeKeepingCentre (btnW, btnH));
        }
    }

    void setAccent (juce::Colour c) { accent = c; onButton.setLedColour (c); repaint(); }

    /* AIR 皮肤切换：tex >= 0 用贴图钮（温润=5 齿轮钮 / 通透=9 白瓷钮），
       -1 恢复银白机身（插件招牌对比色）。贴图模式下容器加宽到 118 对齐其它钮。 */
    void setSkinTex (int tex)
    {
        if (tex == skinTex)
            return;

        skinTex = tex;
        airKnob->setTextureStyle (tex);
        airKnob->setDarkPanel (tex == 9);   // SSL 硬件面板 → 丝印白字
        resized();
    }

    /* Pultec 硬件面板：AIR 卡片同样换成深色模块 → 字标用丝印浅色 */
    void setHardwareSkin (bool dark)
    {
        if (hardwareSkin == dark)
            return;

        hardwareSkin = dark;
        applyLabel();
    }

    /* 厚实 1073：ON 开关换成 AIR 硬件按钮（off 暗 / on 亮，与 LOW/MID/HIGH 同款）；
       传空图 = 恢复默认 LED 开关。点击/联动行为不变。 */
    void setOnHardwareSkin (const juce::Image& offImg, const juce::Image& onImg)
    {
        onButton.setHardwareSkin (offImg, onImg);
        resized();
        repaint();
    }

    LedButton onButton { "ON" };

private:
    /* SSL（白瓷钮）或 Pultec（硬件面板卡）之下，AIR 字标用丝印浅色 */
    void applyLabel()
    {
        const bool dark = hardwareSkin || skinTex == 9;
        airKnob->setDarkPanel (dark);
        airKnob->setLabelSize (14.0f, dark ? juce::Colour (0xffdfe6ec)
                                           : juce::Colour (0xff4a5560));
    }

    int  skinTex = -1;
    bool hardwareSkin = false;
    FusionEQAudioProcessor& proc;
    juce::Colour accent { 0xff0e9bd6 };
    std::unique_ptr<FusionKnob> airKnob;
    bool lastOn = true;      // 初始值取反，确保首轮 timer 立即同步一次
};

/* ---------------------------------------------------------------------------
   Pultec EQP-1A 模块条（温润模式专属，布局严格对照原版硬件）
   LOW  : BOOST / ATTEN 双旋钮在上 + CPS 频点拨杆（20/30/60/100）在下（居中）
   HIGH : BOOST 中上 + BANDWIDTH 左下 + KCS 拨杆（5/8/10/12/16）右下（三角组）
   ATTEN: ATTEN SEL 高切拨杆（5/10/20）—— 原版为独立板块，单占一条
--------------------------------------------------------------------------- */
class PultecStrip : public juce::Component
{
public:
    enum Kind { Low = 0, Band, High, Atten };

    PultecStrip (FusionEQAudioProcessor& p, Kind k)
        : proc (p), kind (k)
    {
        /* 原版硬件外观：面板上不印条带标题，只留控件自带标注 */

        if (kind == Band)
        {
            /* SHARP / BROAD 带宽旋钮（原厂同款贴图，拧到左 SHARP / 右 BROAD） */
            bandKnob = std::make_unique<FusionKnob> (proc.apvts, IDs::plBand, "BANDWIDTH",
                                                     Sweep {}, false, formatBand);
            bandKnob->setTextureStyle (8);
            addAndMakeVisible (*bandKnob);
        }
        else if (kind == Low)
        {
            /* 20/30/60/100 CPS 步进拨杆（长方柄） */
            freqLever = std::make_unique<ToggleLever> (proc, IDs::plLowFreq, 6, 4, 45.0f,
                juce::Point<float> (0.4994f, 0.4637f),
                juce::StringArray { "20", "30", "60", "100" });
            freqLever->setSideScale (0.55f);   // 长拨杆缩小，不盖刻度
            freqLever->setCaption ("LOW FREQUENCY");
            addAndMakeVisible (*freqLever);

            boostKnob = std::make_unique<FusionKnob> (proc.apvts, IDs::plLowBoost, "BOOST",
                                                      Sweep {}, false, formatPultec);
            attenKnob = std::make_unique<FusionKnob> (proc.apvts, IDs::plLowAtten, "ATTEN",
                                                      Sweep {}, false, formatPultec);
            boostKnob->setTextureStyle (5);
            attenKnob->setTextureStyle (5);
            addAndMakeVisible (*boostKnob);
            addAndMakeVisible (*attenKnob);
        }
        else if (kind == Atten)
        {
            /* 独立板块（原版最右）：ATTEN 衰减量旋钮（0-10）+ ATTEN SEL 频点拨杆（5/10/20） */
            attenKnob = std::make_unique<FusionKnob> (proc.apvts, IDs::plHighAttenAmt, "ATTEN",
                                                      Sweep {}, false, formatPultec);
            attenKnob->setTextureStyle (5);
            addAndMakeVisible (*attenKnob);

            attenLever = std::make_unique<ToggleLever> (proc, IDs::plHighAtten, 7, 3, 40.0f,
                juce::Point<float> (0.5f, 0.5198f),
                juce::StringArray { "5", "10", "20" });
            attenLever->setSideScale (0.62f);
            attenLever->setCaption ("ATTEN SEL");
            addAndMakeVisible (*attenLever);
        }
        else if (kind == High)
        {
            boostKnob = std::make_unique<FusionKnob> (proc.apvts, IDs::plHighBoost, "BOOST",
                                                      Sweep {}, false, formatPultec);
            boostKnob->setTextureStyle (5);
            addAndMakeVisible (*boostKnob);

            /* KCS 频点：与 CPS 同款长方拨杆五档步进（严格原版布局，去掉弹出菜单） */
            freqLever = std::make_unique<ToggleLever> (proc, IDs::plHighFreq, 6, 5, 45.0f,
                juce::Point<float> (0.4994f, 0.4637f),
                juce::StringArray { "5", "8", "10", "12", "16" });
            freqLever->setSideScale (0.55f);   // 长拨杆缩小，不盖刻度
            freqLever->setCaption ("HIGH FREQUENCY");
            addAndMakeVisible (*freqLever);

            /* BANDWIDTH 同属高频模块（原版三角组：BOOST / BANDWIDTH / KCS 紧挨）
               素材与 BOOST/ATTEN 统一用最新齿轮钮（tex5） */
            bandKnob = std::make_unique<FusionKnob> (proc.apvts, IDs::plBand, "BANDWIDTH",
                                                     Sweep {}, false, formatBand);
            bandKnob->setTextureStyle (5);
            addAndMakeVisible (*bandKnob);
        }
    }

    void paint (juce::Graphics&) override {}

    /* 硬件面板（Pultec EQP-1A 深色模块）→ 旋钮标注与拨杆刻度换丝印浅字 */
    void setHardwareSkin (bool dark)
    {
        if (boostKnob  != nullptr) boostKnob->setDarkPanel  (dark);
        if (attenKnob  != nullptr) attenKnob->setDarkPanel  (dark);
        if (bandKnob   != nullptr) bandKnob->setDarkPanel   (dark);
        if (freqLever  != nullptr) freqLever->setDarkPanel  (dark);
        if (attenLever != nullptr) attenLever->setDarkPanel (dark);
    }

    void setAccentColour (juce::Colour c)
    {
        if (boostKnob != nullptr) boostKnob->setAccentColour (c);
        if (attenKnob != nullptr) attenKnob->setAccentColour (c);
        if (bandKnob  != nullptr) bandKnob->setAccentColour  (c);
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4, 8);
        r.removeFromTop (6);   // 顶部留白（无标题）

        if (kind == Band)
        {
            /* 带宽旋钮：底部靠右（紧挨 HIGH 组的 BOOST / KCS，组成原版三角组） */
            auto area = r.removeFromBottom (134);
            bandKnob->setBounds (area.withSizeKeepingCentre (120, 128)
                                     .translated ((int) (area.getWidth() * 0.20f), 0));
        }
        else if (kind == Low)
        {
            /* BOOST / ATTEN 旋钮行在上（下移 20px：盘心与 ATTEN SEL 拨杆底座齐平） */
            auto row = r.removeFromTop (118);
            const auto halfW = (row.getWidth() - 10) / 2;
            boostKnob->setBounds (row.removeFromLeft (halfW).translated (0, 20));
            row.removeFromLeft (10);
            attenKnob->setBounds (row.translated (0, 20));

            /* CPS 拨杆底部锚定，与高频模块 KCS 拨杆同一底线、同容器尺寸 */
            freqLever->setBounds (r.removeFromBottom (150).withSizeKeepingCentre (133, 150));
        }
        else if (kind == High)
        {
            /* 高频模块（原版三角组）：BOOST 上中（下移 20px 对齐拨杆底座），
               BANDWIDTH 左下、KCS 右下 */
            boostKnob->setBounds (r.removeFromTop (118).withSizeKeepingCentre (120, 118)
                                      .translated (0, 20));

            r.removeFromTop (6);

            auto row = r.removeFromBottom (150);
            const int halfBot = (row.getWidth() - 6) / 2;
            /* 保持 118×118 尺寸，继续下移：盘面底部与 KCS/CPS 拨杆底部对齐 */
            bandKnob->setBounds (row.removeFromLeft (halfBot).withSizeKeepingCentre (118, 118)
                                     .translated (0, 26));
            row.removeFromLeft (6);
            freqLever->setBounds (row);
        }
        else if (kind == Atten)
        {
            /* 独立板块：与其它模块同构——控件行在顶部，
               ATTEN 旋钮 118×118、ATTEN SEL 拨杆与 CPS/KCS 同尺寸（73px） */
            auto row = r.removeFromTop (118);
            const int halfW = (row.getWidth() - 6) / 2;
            attenKnob->setBounds (row.removeFromLeft (halfW).withSizeKeepingCentre (118, 118)
                                      .translated (0, 20));
            row.removeFromLeft (6);
            attenLever->setBounds (row.withSizeKeepingCentre (133, 118));
        }
    }

private:
    FusionEQAudioProcessor& proc;
    Kind kind;

    std::unique_ptr<FusionKnob> boostKnob, attenKnob, bandKnob;
    std::unique_ptr<ToggleLever> freqLever, attenLever;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PultecStrip)
};
} // namespace Fusion
