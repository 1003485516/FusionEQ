/* =============================================================================
   Fusion EQ  ·  PluginEditor.h
   ============================================================================= */
#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "FusionKnob.h"
#include "SpectrumDisplay.h"
#include "Strips.h"

/* ---------------------------------------------------------------------------
   全局字体：Windows 默认的 Segoe UI 没有 CJK 字形，DirectWrite 渲染不会自动
   回退到别的字体，中文会整段消失。把默认字体统一解析到微软雅黑（拉丁字形
   也齐全），装到 LookAndFeel::setDefaultLookAndFeel 上全局生效。
--------------------------------------------------------------------------- */
class FusionLook : public juce::LookAndFeel_V4
{
public:
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override
    {
        static const juce::String cjkName ("Microsoft YaHei");

        if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()
            || f.getTypefaceName() == "Segoe UI"
            || f.getTypefaceName() == "Segoe UI Variable")
        {
            auto f2 (f);
            f2.setTypefaceName (cjkName);
            return juce::Font::getDefaultTypefaceForFont (f2);
        }

        return juce::Font::getDefaultTypefaceForFont (f);
    }

    /* 模式按钮是单行中文，字号放大；其它按钮可用 "fontH" 属性指定字号 */
    juce::Font getTextButtonFont (juce::TextButton& b, int height) override
    {
        auto f = LookAndFeel_V4::getTextButtonFont (b, height);

        if ((int) b.getProperties().getWithDefault ("modeBtn", 0) != 0)
            f.setHeight (15.0f);

        auto* fh = b.getProperties().getVarPointer ("fontH");
        if (fh != nullptr)
            f.setHeight ((float) *fh);

        return f;
    }
};

/* ---------------------------------------------------------------------------
   通透模式专属标签：「S-EQ」白描边镂空艺术字（Chakra 压缩科技风）
   只在 SSL（通透）模式显示，替换其它模式的「模式标签」文字。
--------------------------------------------------------------------------- */
class SeqBadge : public juce::Component
{
public:
    SeqBadge() { setInterceptsMouseClicks (false, false); }

    void setAccent (juce::Colour c) { accent = c; repaint(); }

    void setText (const juce::String& t) { text = t; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        /* 标签底：与其它模式的模式标签同一套强调色渐变 */
        juce::ColourGradient cg (accent.brighter (0.08f), 0.0f, r.getY(),
                                 accent.darker  (0.12f), 0.0f, r.getBottom(), false);
        g.setGradientFill (cg);
        g.fillRoundedRectangle (r, 6.0f);

        /* 方案③：Michroma 风细宽字 + 淡蓝辉光（精密仪表盘刻字）。
           Michroma 无系统字体，用 Bahnschrift 常规字重横向拉伸近似细宽感 */
        juce::Font f (juce::Font ("Bahnschrift", 19.0f, juce::Font::plain));
        f = f.withHorizontalScale (1.24f).withExtraKerningFactor (0.14f);

        juce::GlyphArrangement ga;
        ga.addLineOfText (f, text, 0.0f, 0.0f);

        juce::Path p;
        ga.createPath (p);

        const auto pb = p.getBounds();
        if (pb.isEmpty() || pb.getHeight() <= 0.0f)
            return;

        const auto scale = juce::jmin ((r.getWidth()  - 30.0f) / pb.getWidth(),
                                       (r.getHeight() - 10.0f) / pb.getHeight());
        p.applyTransform (juce::AffineTransform::scale (scale)
                            .translated (r.getCentreX() - pb.getCentreX() * scale,
                                         r.getCentreY() - pb.getCentreY() * scale));

        /* 双层淡蓝辉光（模拟 CSS 双层 text-shadow：近距亮晕 + 远距冷晕），
           颜色由模式强调色派生，保证各模式下底/字/辉光同色系 */
        juce::Path halo;
        juce::PathStrokeType (9.0f, juce::PathStrokeType::mitered,
                              juce::PathStrokeType::rounded).createStrokedPath (halo, p);
        g.setColour (accent.brighter (0.45f).withAlpha (0.30f));
        g.fillPath (halo);

        halo.clear();
        juce::PathStrokeType (4.0f, juce::PathStrokeType::mitered,
                              juce::PathStrokeType::rounded).createStrokedPath (halo, p);
        g.setColour (accent.brighter (0.75f).withAlpha (0.55f));
        g.fillPath (halo);

        /* 主体：冰白刻字 */
        g.setColour (juce::Colour (0xffeaf6ff));
        g.fillPath (p);
    }

private:
    juce::Colour accent { 0xff8464c8 };
    juce::String text { "S-EQ" };
};

/* ---------------------------------------------------------------------------
   主编辑器
--------------------------------------------------------------------------- */
/* ---------------------------------------------------------------------------
   频谱收起/展开的小箭头按钮（人字形，悬停浮现底色，方向随状态翻转）
--------------------------------------------------------------------------- */
class ChevronButton : public juce::Button
{
public:
    ChevronButton() : juce::Button ("scopeToggle") {}

    void setOpen (bool open) { up = open; repaint(); }

    /* 深色硬件外观：悬停底色与箭头转浅色丝印 */
    void setDarkSkin (bool d)
    {
        if (dark == d)
            return;

        dark = d;
        repaint();
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (5.0f, 6.0f);

        if (over || down)
        {
            g.setColour (juce::Colour (down ? (dark ? 0x36ffffff : 0x24000000)
                                            : (dark ? 0x22ffffff : 0x18000000)));
            g.fillRoundedRectangle (r, 6.0f);
        }

        juce::Path p;
        const float cy = r.getCentreY();

        if (up)     // 展开 → 点按收起（箭头朝上）
        {
            p.startNewSubPath (r.getX() + 2.0f, cy + 3.5f);
            p.lineTo (r.getCentreX(), cy - 4.5f);
            p.lineTo (r.getRight() - 2.0f, cy + 3.5f);
        }
        else        // 收起 → 点按展开（箭头朝下）
        {
            p.startNewSubPath (r.getX() + 2.0f, cy - 3.5f);
            p.lineTo (r.getCentreX(), cy + 4.5f);
            p.lineTo (r.getRight() - 2.0f, cy - 3.5f);
        }

        g.setColour (juce::Colour (dark ? (down ? 0xff8a949d : 0xffb4bdc5)
                                        : (down ? 0xff39424c : 0xff5c666f)));
        g.strokePath (p, juce::PathStrokeType (2.2f,
                                               juce::PathStrokeType::mitered,
                                               juce::PathStrokeType::rounded));
    }

private:
    bool up   = true;
    bool dark = false;
};

class FusionEQAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    explicit FusionEQAudioProcessorEditor (FusionEQAudioProcessor&);
    ~FusionEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshMode();                 // 模式变化 → 换强调色与说明文字
    void buildStrips();
    void syncToggleText();              // 自动化改写开关 → 同步按钮文字

    FusionEQAudioProcessor& processor;

    /* 逻辑画布：所有控件装在这里，按 1180×932 设计尺寸布局；
       窗口拖拽缩放时对整块画布做仿射缩放（见 resized） */
    juce::Component canvas;
    float uiScale = 1.0f;               // 物理 / 逻辑尺寸比

    FusionLook fusionLook;              // 全局字体（含 CJK）解析

    /* --- 顶栏 --- */


    struct ModeButton : public juce::TextButton
    {
        int modeIndex = 0;
    };
    juce::OwnedArray<ModeButton> modeButtons;

    Fusion::LedButton bypassButton { "BYPASS" };
    Fusion::GemButton bypassGem;        // 温润模式 ATTEN 板块：宝石旁通灯（联动 BYPASS）

    /* --- 模式标签（说明文字已移除） --- */
    juce::Label modeTag;
    SeqBadge seqBadge;                  // 通透/温润模式：S-EQ · P-EQ 艺术字标签

    /* --- 频谱 --- */
    SpectrumDisplay spectrum { processor };

    /* 频谱收起/展开（带过渡动画，见 timerCallback / resized） */
    ChevronButton scopeToggle;
    bool  scopeOpen = true;     // 目标状态
    float scopeAnim = 1.0f;     // 1 = 展开，0 = 收起（动画插值）
    int   scopeNowH = 360;      // 频谱当前显示高度（resized 更新，paint 使用）

    /* --- 机架 --- */
    Fusion::HpfStrip hpfStrip { processor.apvts };
    juce::OwnedArray<Fusion::BandStrip> bandStrips;
    Fusion::AirStrip airStrip { processor };

    /* Pultec EQP-1A 三段式（温润模式专属） */
    Fusion::PultecStrip plLowStrip  { processor, Fusion::PultecStrip::Low };
    Fusion::PultecStrip plHighStrip { processor, Fusion::PultecStrip::High };
    Fusion::PultecStrip plAttenStrip { processor, Fusion::PultecStrip::Atten };

    /* --- 底栏 --- */
    std::unique_ptr<Fusion::FusionKnob> outKnob;      // 仅保留 OUTPUT
    juce::TextButton autoGainButton { "AUTO GAIN" };
    juce::TextButton osButton       { "OVERSAMPLE" };

    /* --- 开关按钮与宿主参数双向同步 --- */
    using BtnAttach = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<BtnAttach> hpfAttach, lpAttach, airAttach, autoGainAttach;
    std::vector<std::unique_ptr<BtnAttach>> bandAttach;

    Fusion::Mode lastMode = Fusion::Mode::Count;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FusionEQAudioProcessorEditor)
};
