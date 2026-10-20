/* =============================================================================
   Fusion EQ  ·  PluginEditor.cpp
   ============================================================================= */
#include "PluginEditor.h"
#include "ParamIDs.h"

namespace
{
    constexpr int unitWidth  = 1180;
    constexpr int unitHeight = 932;

    constexpr int topBarH = 66;
    constexpr int descH   = 54;
    constexpr int scopeH  = 360;
    constexpr int rackH   = 366;
    constexpr int footH   = 76;
    constexpr int rackGap = 8;

    const juce::Colour cFace   { 0xfff4f6f8 };
    const juce::Colour cFace2  { 0xffe8ecf0 };
    const juce::Colour cLine   { 0xffc3cad2 };
    const juce::Colour cLineDk { 0xffa8b1ba };
    const juce::Colour cTxt    { 0xff1d242b };
    const juce::Colour cTxt2   { 0xff5c666f };
    const juce::Colour cTxt3   { 0xff8b949c };

    const juce::StringArray& bandNames = Fusion::BAND_NAMES();
}

/* ============================ 构造 ============================ */
FusionEQAudioProcessorEditor::FusionEQAudioProcessorEditor (FusionEQAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    /* 注意：setSize 必须在所有子组件创建完成之后调用！
       setSize 会同步触发 resized()，而 resized() 会访问 inKnob/outKnob 等成员，
       提前调用就是对空 unique_ptr 解引用（Cubase 加载即崩溃的根因）。 */
    /* 拖拽窗口边缘任意缩放：原生边框 + 等比约束（0.5×~2×），
       内容按 1180×932 逻辑画布整体仿射缩放（见 resized） */
    setResizable (true, false);
    setResizeLimits (unitWidth / 2, unitHeight / 2, unitWidth * 2, unitHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) unitWidth / (double) unitHeight);

    addAndMakeVisible (canvas);     // 逻辑画布（其余控件全部挂到这里）

    /* 全局字体解析：任何绘制字形前都会经过默认 LookAndFeel（见 juce_Font.cpp），
       所以必须装在这里，中文才不会因为 Segoe UI 缺字形而整段消失 */
    juce::LookAndFeel::setDefaultLookAndFeel (&fusionLook);

    /* ---- 顶栏 ---- */
    /* logoSub 已改由 paint() 直接绘制（动态字距撑满），不再作为控件 */

    const auto& specs = Fusion::getModeSpecs();

    for (int i = 0; i < Fusion::numModes; ++i)
    {
        auto* b = modeButtons.add (new ModeButton());
        b->modeIndex = i;
        b->getProperties().set ("modeBtn", 1);
        b->setButtonText (specs[(size_t) i].cn);       // 单行中文模式名
        b->setClickingTogglesState (false);
        b->onClick = [this, i]()
        {
            if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (
                    processor.apvts.getParameter (IDs::mode)))
            {
                c->beginChangeGesture();
                c->setValueNotifyingHost (c->convertTo0to1 ((float) i));
                c->endChangeGesture();
            }
            refreshMode();
        };
        canvas.addAndMakeVisible (b);
    }

    bypassButton.setClickingTogglesState (true);
    bypassButton.setShowOnOffText (false);              // 保留 BYPASS/BYPASSED 文本
    bypassButton.setLedColour (juce::Colour (0xff2f9e6a)); // 处理中绿灯亮
    bypassButton.setLedPolarity (false);                // 反接：旁通时灭灯
    bypassButton.onClick = [this]()
    {
        const bool on = bypassButton.getToggleState();
        bypassButton.setButtonText (on ? "BYPASSED" : "BYPASS");
        bypassGem.setToggleState (on, juce::dontSendNotification);   // 宝石灯联动
        processor.setBypassed (on);
    };
    canvas.addAndMakeVisible (bypassButton);

    /* 宝石旁通灯（温润模式 ATTEN 板块）：点击 = 切换 BYPASS，与右上角按钮双向同步 */
    bypassGem.onClick = [this]()
    {
        bypassButton.setToggleState (! bypassButton.getToggleState(),
                                     juce::sendNotificationSync);
    };
    canvas.addAndMakeVisible (bypassGem);
    bypassGem.setVisible (false);       // 仅温润模式显示（refreshMode 控制）

    /* ---- 模式说明 ---- */
    modeTag.setFont (juce::Font (11.5f, juce::Font::bold));
    modeTag.setColour (juce::Label::textColourId, juce::Colours::white);
    modeTag.setJustificationType (juce::Justification::centred);
    canvas.addAndMakeVisible (modeTag);

    seqBadge.setVisible (false);        // 仅通透/温润模式显示（refreshMode 控制）
    canvas.addAndMakeVisible (seqBadge);

    /* 频谱收起/展开按钮：点击后整窗高度随缓动动画收起频谱 */
    scopeToggle.setOpen (true);
    scopeToggle.setTooltip (Fusion::U8 ("\u6536\u8d77 / \u5c55\u5f00\u9891\u8c31"));
    scopeToggle.onClick = [this]()
    {
        scopeOpen = ! scopeOpen;
        scopeToggle.setOpen (scopeOpen);
    };
    canvas.addAndMakeVisible (scopeToggle);

    /* 模式说明文字已按需求全部移除（顶部只留模式标签 / S-EQ · P-EQ 艺术字） */

    /* ---- 频谱 ---- */
    canvas.addAndMakeVisible (spectrum);

    /* ---- 机架 ---- */
    buildStrips();

    /* ---- 底栏 ---- */
    outKnob = std::make_unique<Fusion::FusionKnob> (processor.apvts, IDs::output, "OUTPUT",
                                                    Fusion::Sweep {}, true, Fusion::formatGain);
    canvas.addAndMakeVisible (*outKnob);

    /* 自动增益补偿开关（取代原 INPUT 旋钮） */
    autoGainButton.setClickingTogglesState (true);
    autoGainButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2f9e6a));
    autoGainButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    autoGainAttach = std::make_unique<BtnAttach> (processor.apvts, IDs::autoGain, autoGainButton);
    canvas.addAndMakeVisible (autoGainButton);

    /* 过采样倍率：点击弹出菜单直接选择 OFF / X2 / X4 / X8 / X16（英文显示）。
       OFF = 基率直通零延迟；更高倍率音质更好，延迟相应增加。 */
    osButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3a6fd8));
    osButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    osButton.onClick = [this]()
    {
        const auto cur = juce::jlimit (0, 4, juce::roundToInt (
            processor.apvts.getRawParameterValue (IDs::osMode)->load()));

        juce::PopupMenu m;
        m.addSectionHeader ("OVERSAMPLING");
        m.addItem (1, "OFF",            true, cur == 0);
        m.addItem (2, "X2",             true, cur == 1);
        m.addItem (3, "X4",             true, cur == 2);
        m.addItem (4, "X8",             true, cur == 3);
        m.addItem (5, "X16",            true, cur == 4);

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&osButton),
            [this] (int result)
            {
                if (result <= 0)
                    return;

                if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (
                        processor.apvts.getParameter (IDs::osMode)))
                {
                    c->beginChangeGesture();
                    c->setValueNotifyingHost (c->convertTo0to1 ((float) (result - 1)));
                    c->endChangeGesture();
                }
            });
    };
    canvas.addAndMakeVisible (osButton);

    refreshMode();

    /* 一切就绪后才允许设置尺寸（触发首次 resized()） */
    setSize (unitWidth, unitHeight);
    startTimerHz (24);
}

FusionEQAudioProcessorEditor::~FusionEQAudioProcessorEditor()
{
    stopTimer();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

/* 模式可能来自宿主自动化 / 预置，定时检查以同步 UI */
void FusionEQAudioProcessorEditor::timerCallback()
{
    /* 频谱收起/展开动画：指数趋近目标 + smoothstep 缓动，驱动整窗高度变化 */
    {
        const float target = scopeOpen ? 1.0f : 0.0f;

        if (std::abs (scopeAnim - target) > 0.0005f)
        {
            const float dt = 1.0f / 24.0f;
            const float k  = 1.0f - std::exp (-dt / 0.055f);   // ~55ms 时间常数
            scopeAnim += (target - scopeAnim) * k;

            if (std::abs (scopeAnim - target) < 0.02f)
                scopeAnim = target;

            const float e = scopeAnim * scopeAnim * (3.0f - 2.0f * scopeAnim);
            const int   collapsedH = unitHeight - scopeH - 8;
            const int   newH = collapsedH + juce::roundToInt ((unitHeight - collapsedH) * e);

            /* 逻辑高度 → 物理窗口高度（× uiScale）；动画不受约束器限制，
               结束后把拖拽等比约束同步到目标状态的比例 */
            const int phyH = juce::roundToInt ((float) newH * uiScale);

            if (phyH != getHeight())
                setSize (getWidth(), phyH);
            else
                resized();

            getConstrainer()->setFixedAspectRatio (
                (double) unitWidth / (double) juce::jmax (1, newH));
        }
    }

    if (processor.getMode() != lastMode)
        refreshMode();

    syncToggleText();

    /* 宝石灯永远跟随 BYPASS 按钮状态（防手动改按钮后不同步） */
    bypassGem.setToggleState (bypassButton.getToggleState(), juce::dontSendNotification);

    /* 过采样按钮：文字与颜色跟随参数（可能被宿主自动化改变） */
    const auto osIdx = juce::jlimit (0, 4, juce::roundToInt (
        processor.apvts.getRawParameterValue (IDs::osMode)->load()));

    const juce::String osTxt = osIdx == 0 ? "OVERSAMPLE OFF"
                                          : "OVERSAMPLE X" + juce::String (1 << osIdx);

    if (osButton.getButtonText() != osTxt)
        osButton.setButtonText (osTxt);

    osButton.setToggleState (osIdx > 0, juce::dontSendNotification);
}

/* ============================ 模式刷新 ============================ */
void FusionEQAudioProcessorEditor::refreshMode()
{
    const auto m     = processor.getMode();
    const auto& spec = Fusion::getSpec (m);
    const auto accent = juce::Colour (spec.accent);

    /* ------- 模式参数隔离 -------
       切换模式：把旧模式的调节存进它的快照，再把新模式自己的快照写回参数。
       频段 / 低切 / SSL 滤波 / Magic Air 各模式记忆各自的设置，互不带走。 */
    if (lastMode != m)
    {
        if (lastMode != Fusion::Mode::Count)
        {
            processor.captureModeState ((int) lastMode);
            processor.restoreModeState ((int) m);
        }
        else
            processor.captureModeState ((int) m);   // 首次刷新：以宿主恢复的当前值作为本模式快照
    }

    spectrum.setAccentColour (accent);
    hpfStrip.setAccent (accent);
    airStrip.setAccent (accent);

    /* Manley（温暖）= 电子管皮肤；SSL（通透）= 4000E 4 段 + 四色贴图旋钮；
       厚实（1073）/ 硬朗（550L）= 真机硬件模拟：频谱同为纯观察视图 */
    const bool tube  = m == Fusion::Mode::Manley;
    const bool isSSL = m == Fusion::Mode::SSL;
    const bool isPultec = m == Fusion::Mode::Pultec;
    /* 硬件模拟形态：频谱为纯观察视图（通透/厚实/硬朗/温暖，无频点、无鼠标交互） */
    const bool isHWmode = isSSL || tube
                          || m == Fusion::Mode::Neve || m == Fusion::Mode::API;

    spectrum.setSslPalette (isSSL);      // SSL：频点颜色跟随四色旋钮
    spectrum.setNodesVisible (! isHWmode);

    /* 频谱面板皮肤 = 各模式机架面板色（只动频谱观感，机架零改动） */
    spectrum.setPanelSkin (isSSL          ? 1
                           : isPultec     ? 2
                           : m == Fusion::Mode::Neve   ? 3
                           : m == Fusion::Mode::API    ? 4
                           : tube         ? 5
                                          : 0);

    /* 深色硬件外观：顶栏/标题条/底栏随模式转深（画在 paint），控件跟随换肤 */
    const bool darkUI = isHWmode || isPultec;
    scopeToggle.setDarkSkin (darkUI);
    if (outKnob != nullptr)
        outKnob->setDarkPanel (darkUI);
    osButton.setColour (juce::TextButton::buttonOnColourId,
                        darkUI ? accent : juce::Colour (0xff3a6fd8));

    /* Pultec 模块条带：只在温润模式显示；温润模式无低切（原机无此功能）
       低频模块 = BOOST/ATTEN/CPS；高频模块 = BOOST/BANDWIDTH/KCS；ATTEN SEL = 独立板块 */
    hpfStrip.setVisible (! isPultec);
    hpfStrip.setSslFilter (isSSL);   // SSL：LOW CUT → 原版 FILTER（HP/LP 红钮）
    if (m == Fusion::Mode::Manley)
        hpfStrip.setMpFilt (true);   // 温暖：MP 高/低切（OFF 在旋钮端点档）
    else
    {
        hpfStrip.setMpFilt (false);
        hpfStrip.setApi215 (m == Fusion::Mode::API);        // 硬朗：215L 双滤波（LO-PASS/HI-PASS）
    }
    hpfStrip.setStepperVisible (m == Fusion::Mode::Neve);   // 步进蓝钮仅厚实模式；屏幕给干净/温暖/硬朗
    hpfStrip.setTitlesVisible (m != Fusion::Mode::Neve
                               && m != Fusion::Mode::Manley);   // 厚实/温暖：硬件无丝印标题
    hpfStrip.setHwButtonSkin (m == Fusion::Mode::Neve || m == Fusion::Mode::API);  // 厚实/硬朗：ON 换硬件 HP 按钮
    plLowStrip.setVisible   (isPultec);
    plHighStrip.setVisible  (isPultec);
    plAttenStrip.setVisible (isPultec);

    if (isPultec)
    {
        plLowStrip.setAccentColour   (accent);
        plHighStrip.setAccentColour  (accent);
        plAttenStrip.setAccentColour (accent);

        /* Pultec 硬件皮肤：三个模块条断电替换为 EQP-1A 石墨哑光面板 + 丝印浅字 */
        plLowStrip.setHardwareSkin   (true);
        plHighStrip.setHardwareSkin  (true);
        plAttenStrip.setHardwareSkin (true);

        airStrip.setSkinTex (5);       // AIR 换 BOOST 同款齿轮钮，卡片行风格统一
    }
    else
    {
        /* 非 Pultec：模块条恢复常规样式（下一次进入 Pultec 会重新下发） */
        plLowStrip.setHardwareSkin   (false);
        plHighStrip.setHardwareSkin  (false);
        plAttenStrip.setHardwareSkin (false);

        /* 通透（SSL）：AIR 用白瓷钮与四色贴图钮一套质感；
           温暖（Manley）：AIR 换成与六段一致的黑色电子管钮；
           厚实：红色玻璃环钮；硬朗：API 青芯银裙钮；其余恢复银白机身 */
        airStrip.setSkinTex (isSSL ? 9 : (tube ? 4
                                       : m == Fusion::Mode::Neve ? 15
                                       : m == Fusion::Mode::API  ? 18 : -1));
    }

    /* Pultec/厚实/硬朗：AIR 卡片进深色模块，字标转丝印浅色 */
    airStrip.setHardwareSkin (isPultec || m == Fusion::Mode::Neve
                                  || m == Fusion::Mode::API);

    /* 厚实 1073 / 硬朗 550L：AIR 的 ON 开关统一成硬件文字按钮（复用 HP 素材改字）；
       其它模式恢复 LED 开关 */
    if (m == Fusion::Mode::Neve || m == Fusion::Mode::API)
        airStrip.setOnHardwareSkin (
            juce::ImageFileFormat::loadFrom (BinaryData::btn_air_off_png, (size_t) BinaryData::btn_air_off_pngSize),
            juce::ImageFileFormat::loadFrom (BinaryData::btn_air_on_png,  (size_t) BinaryData::btn_air_on_pngSize));
    else
        airStrip.setOnHardwareSkin ({}, {});

    bypassGem.setVisible (isPultec);       // 宝石旁通灯只在温润模式 ATTEN 板块显示

    static const juce::String sslEn[4] { "LF", "LMF", "HMF", "HF" };
    static const juce::String sslCn[4] {
        Fusion::U8 ("\u4f4e\u9891"),   Fusion::U8 ("\u4e2d\u4f4e\u9891"),
        Fusion::U8 ("\u4e2d\u9ad8\u9891"), Fusion::U8 ("\u9ad8\u9891")
    };

    int sslSlot = 0;
    int neveSlot = 0;
    int apiSlot = 0;
    int mpSlot = 0;

    for (auto* s : bandStrips)
    {
        const bool active = Fusion::isBandActive (m, s->index);
        s->setVisible (active);        // SSL 4 段：隐藏 LOW-MID / PRESENCE

        s->setHardwareSkin (isSSL);    // SSL：深灰硬件模块面板 + 丝印白字
        /* 原版 SSL 无段开关：SSL 模式隐藏 ON/OFF（常开），其它模式恢复 */
        s->setSwitchVisible (! isSSL);
        s->setBigRows (isSSL);       // SSL：三排旋钮放大

        /* 先无条件复位成"完整菜单 + 常规布局"，SSL 白名单只对下面活动段下发。
           这样离开 SSL 后任何频段（包括 SSL 期间被隐藏的段）都不可能残留限限。 */
        s->setCurveMode (-1);
        s->setTypeMenuLocked (false);                   // 解锁：恢复 ▾ 与可点性
        s->setAllowedTypes ({ });                       // 白名单清空 = 完整九类
        s->setFreqRange (Fusion::fMin, 20000.0f);       // 非 SSL：上限保持 20k（22k 仅 SSL HF）
        s->setTypeBadgeVisible (true);                  // 其它模式：类型铭牌显示
        s->setQVisible (true);                          // Q 旋钮恢复（Neve 再按需隐藏）
        s->setFreqKnobVisible (true);                   // 频点旋钮恢复（Neve HIGH 再按需隐藏）
        s->setFreqSnap ({});                            // 清离散档（Neve 再按需下发）
        s->setFreqLabelsVisible (true);                 // 频点钮名称/读数恢复（Neve 再按需隐藏）
        s->setTitlesVisible (true);                     // 条带标题恢复（Neve 再按需隐藏）
        s->setOnHardwareSkin ({}, {});                  // ON 开关恢复 LED 样式（Neve 再换硬件钮）
        s->setKnobLabelsDark (false);                   // 旋钮标签恢复深色（Neve 再转丝印浅色）
        s->setContentCentered (false);                  // 内容居中复位（API 再按需开启）
        s->setApiBlueFreqNums (false);                  // 频点数值恢复中灰（API 再转招牌蓝）
        s->setMpMode (false, false);                    // 拨杆复位（温暖 MP 再按需开启）
        s->setGainValueOnTop (false);                   // GAIN 读数回钮下（温暖 MP 再移钮上）
        s->setGainNameVisible (true);                   // GAIN 名称恢复（温暖 MP 再隐藏）

        if (active)
        {
            if (isSSL)
            {
                s->setBandTitles (sslEn[sslSlot], sslCn[sslSlot]);
                s->setTextureStyle (sslSlot);      // LF黑 / LMF绿 / HMF蓝 / HF紫

                /* 频点范围照原版 4000 各段收敛 */
                static constexpr float sslFreqLo[4] { 40.0f, 200.0f, 600.0f, 1500.0f };
                static constexpr float sslFreqHi[4] { 600.0f, 2000.0f, 7000.0f, 22000.0f };
                s->setFreqRange (sslFreqLo[sslSlot], sslFreqHi[sslSlot]);

                if (sslSlot == 0)
                {
                    /* LF：∩ 曲线按钮取代 Q 旋钮与类型菜单（SSL 4000 原始设计）
                       未按 = Low Shelf（原版搁架）；按下 = 钟形（琥珀亮灯），Q 固定 */
                    s->setCurveMode ((int) Fusion::BandType::LowShelf);
                    s->setAllowedTypes ({ 0, 1 });
                }
                else if (sslSlot == 3)
                {
                    /* HF：Q 旋钮换成 BELL 按钮（模拟 4000 硬件）
                       未按 = High Shelf；按下 = 钟形 */
                    s->setCurveMode ((int) Fusion::BandType::HighShelf);
                    s->setAllowedTypes ({ 0, 3 });
                }
                else
                {
                    /* LMF/HMF 固定 BELL（Bell=0）：锁菜单 + 隐藏铭牌 */
                    s->setTypeBadgeVisible (false);
                    s->setTypeMenuLocked (true);
                    s->setAllowedTypes ({ 0 });
                }
            }
            else if (m == Fusion::Mode::Neve)
            {
                /* NEVE 1073 三段式：LOW / MID / HIGH（活动频段 0 / 2 / 5 按序对应槽位） */
                static const juce::String en3[3] { "LOW", "MID", "HIGH" };
                static const juce::String cn3[3] { Fusion::U8 ("\u4f4e\u9891"), Fusion::U8 ("\u4e2d\u9891"), Fusion::U8 ("\u9ad8\u9891") };
                static const int        ty3[3] { (int) Fusion::BandType::LowShelf,
                                                 (int) Fusion::BandType::Bell,
                                                 (int) Fusion::BandType::HighShelf };

                s->setBandTitles (en3[neveSlot], cn3[neveSlot]);
                s->setKnobTextures (16, 11, 11);            // 频点=蓝玻璃步进钮 / 增益=Neve 米色钮（Q 已隐藏）
                s->setTypeBadgeVisible (false);             // 1073 硬件模拟：不显示 EQ 类型铭牌
                s->setTitlesVisible (false);                // 1073 硬件模拟：不显示条带标题
                s->setTypeMenuLocked (true);
                s->setKnobLabelsDark (true);                // 1073 深面板：GAIN 名称/读数转丝印浅色
                s->setAllowedTypes ({ ty3[neveSlot] });     // 类型固定（1073 无类型选择）

                /* ON 开关换成 1073 硬件文字按钮（LOW/MID/HIGH，off 暗 / on 亮） */
                {
                    static const struct { const char* off, * on; } hwBtn[3] = {
                        { BinaryData::btn_low_off_png,  BinaryData::btn_low_on_png  },
                        { BinaryData::btn_mid_off_png,  BinaryData::btn_mid_on_png  },
                        { BinaryData::btn_high_off_png, BinaryData::btn_high_on_png },
                    };
                    static const int hwBtnSz[3][2] = {
                        { BinaryData::btn_low_off_pngSize,  BinaryData::btn_low_on_pngSize  },
                        { BinaryData::btn_mid_off_pngSize,  BinaryData::btn_mid_on_pngSize  },
                        { BinaryData::btn_high_off_pngSize, BinaryData::btn_high_on_pngSize },
                    };
                    s->setOnHardwareSkin (
                        juce::ImageFileFormat::loadFrom (hwBtn[neveSlot].off, (size_t) hwBtnSz[neveSlot][0]),
                        juce::ImageFileFormat::loadFrom (hwBtn[neveSlot].on,  (size_t) hwBtnSz[neveSlot][1]));
                }

                if (neveSlot == 0)
                {
                    s->setFreqSnap ({ 35.0f, 60.0f, 110.0f, 220.0f });                    // LOW 四档
                    s->setFreqLabelsVisible (false);    // 硬件模拟：数值读数冗余，档位值在环绕刻度
                }
                else if (neveSlot == 1)
                {
                    s->setFreqSnap ({ 360.0f, 700.0f, 1600.0f, 3200.0f,
                                      4800.0f, 7200.0f, 10000.0f });                      // MID 七档
                    s->setFreqLabelsVisible (false);
                }
                else
                {
                    s->setFreqKnobVisible (false);      // HIGH 固定 12 kHz，无频点钮
                    s->forceFreqHz (12000.0f);
                }

                s->setQVisible (false);                     // 1073：Q 硬件固定，不开放
                ++neveSlot;
            }
            else if (m == Fusion::Mode::Manley)
            {
                /* Manley Massive Passive 母带被动均衡：4 段（照原机面板）——
                   每段 = BOOST/OUT/CUT + SHELF/BELL 拨杆、GAIN 钮、11 档 FREQUENCY 选择；
                   Q 按原机固定特性（不开放），卡片标题 = 原机频段量程丝印 */
                static const juce::String mpEn[4] { "22 - 1K", "82 - 3K9",
                                                    "220 - 10K", "560 - 27K" };
                s->setBandTitles (mpEn[mpSlot], "");
                s->setKnobTextures (4, 4, -1);          // 原机黑色电子管钮（GAIN/FREQ 同款）

                switch (mpSlot)                          // 11 档频点（照原机丝印）
                {
                    case 0:  s->setFreqSnap ({ 22.0f, 33.0f, 47.0f, 68.0f, 100.0f,
                                              150.0f, 220.0f, 330.0f, 470.0f,
                                              680.0f, 1000.0f });            break;
                    case 1:  s->setFreqSnap ({ 82.0f, 120.0f, 180.0f, 270.0f, 390.0f,
                                              560.0f, 820.0f, 1200.0f, 1800.0f,
                                              2700.0f, 3900.0f });           break;
                    case 2:  s->setFreqSnap ({ 220.0f, 330.0f, 470.0f, 680.0f, 1000.0f,
                                              1500.0f, 2200.0f, 3300.0f, 4700.0f,
                                              6800.0f, 10000.0f });          break;
                    default: s->setFreqSnap ({ 560.0f, 820.0f, 1200.0f, 1800.0f, 2700.0f,
                                              3900.0f, 5600.0f, 8200.0f, 12000.0f,
                                              16000.0f, 27000.0f });         break;
                }
                s->setFreqLabelsVisible (false);        // 档位值印在钮周（环绕刻度）
                s->setQVisible (false);                 // Q 按原机硬件固定，不开放
                s->setTypeBadgeVisible (false);         // 原机无类型铭牌（拨杆承担）
                s->setTypeMenuLocked (true);
                s->setSwitchVisible (false);            // ON 由 BOOST/OUT/CUT 拨杆承担
                s->setHardwareSkin (true);              // 黑色插入面板：丝印浅字
                /* 前两段可 LowShelf，后两段可 HighShelf（原机规则），全部可 BELL */
                s->setAllowedTypes ({ mpSlot < 2 ? (int) Fusion::BandType::LowShelf
                                                 : (int) Fusion::BandType::HighShelf,
                                     (int) Fusion::BandType::Bell });
                s->setMpMode (true, mpSlot >= 2);
                s->setGainValueOnTop (true);            // 读数移钮上：下方留给频点环绕刻度
                s->setGainNameVisible (false);          // "GAIN" 丝印删掉（拨杆行已表意）
                ++mpSlot;
            }
            else if (m == Fusion::Mode::API)
            {
                /* API 550L 硬件模拟：4 段（LF/LMF/HMF/HF），频点为离散档位选择，
                   Q 不开放（音频端按比例 Q 模拟），频点读数由环绕刻度承担 */
                static const char* apiEn[4]  { "LF", "LMF", "HMF", "HF" };
                static const juce::String apiCn[4] {
                    Fusion::U8 ("\u4f4e\u9891"), Fusion::U8 ("\u4e2d\u4f4e\u9891"),
                    Fusion::U8 ("\u4e2d\u9ad8\u9891"), Fusion::U8 ("\u9ad8\u9891")
                };
                s->setBandTitles (apiEn[apiSlot], apiCn[apiSlot]);
                s->setKnobTextures (13, 14, -1);        // FREQ=API 银钮 / GAIN=API 蓝钮

                switch (apiSlot)
                {
                    case 0:  s->setFreqSnap ({ 30.0f, 40.0f, 50.0f, 100.0f,
                                               200.0f, 300.0f, 400.0f });            break;  // LF 七档
                    case 1:  s->setFreqSnap ({ 75.0f, 150.0f, 180.0f, 240.0f,
                                               500.0f, 700.0f, 1000.0f });           break;  // LMF 七档
                    case 2:  s->setFreqSnap ({ 800.0f, 1500.0f, 3000.0f,
                                               5000.0f, 8000.0f, 10000.0f });        break;  // HMF 六档
                    default: s->setFreqSnap ({ 2500.0f, 5000.0f, 7000.0f, 9000.0f,
                                               10000.0f, 12500.0f, 15000.0f, 20000.0f }); break; // HF 八档
                }
                s->setFreqLabelsVisible (false);        // 档位值印在钮周（环绕刻度）
                s->setContentCentered (true);           // 与低切卡一致：内容整体垂直居中
                s->setApiBlueFreqNums (true);           // 550L 招牌蓝字：频点数值用 API 蓝
                s->setTypeBadgeVisible (false);         // 550L 硬件模拟：不显示 EQ 类型铭牌
                s->setTitlesVisible (false);            // 550L 硬件模拟：不显示条带标题（同 1073）
                s->setHardwareSkin (true);              // 550L 深面板：旋钮名称/读数转丝印浅色
                s->setTypeMenuLocked (true);
                /* 550L 硬件形态固定各段曲线类型：LF/HF 搁架、LMF/HMF 钟形 */
                {
                    static const int apiTy[4] { (int) Fusion::BandType::LowShelf,
                                                (int) Fusion::BandType::Bell,
                                                (int) Fusion::BandType::Bell,
                                                (int) Fusion::BandType::HighShelf };
                    s->setAllowedTypes ({ apiTy[apiSlot] });
                }
                s->setQVisible (false);                 // 550L 无 Q 旋钮（比例 Q 内建）

                /* 550L：ON 开关换成 API 硬件文字按钮（LF/LMF/HMF/HF，off 暗 / on 亮） */
                {
                    static const struct { const char* off, * on; } apiBtn[4] = {
                        { BinaryData::btn_lf_off_png,  BinaryData::btn_lf_on_png  },
                        { BinaryData::btn_lmf_off_png, BinaryData::btn_lmf_on_png },
                        { BinaryData::btn_hmf_off_png, BinaryData::btn_hmf_on_png },
                        { BinaryData::btn_hf_off_png,  BinaryData::btn_hf_on_png  },
                    };
                    static const int apiBtnSz[4][2] = {
                        { BinaryData::btn_lf_off_pngSize,  BinaryData::btn_lf_on_pngSize  },
                        { BinaryData::btn_lmf_off_pngSize, BinaryData::btn_lmf_on_pngSize },
                        { BinaryData::btn_hmf_off_pngSize, BinaryData::btn_hmf_on_pngSize },
                        { BinaryData::btn_hf_off_pngSize,  BinaryData::btn_hf_on_pngSize  },
                    };
                    s->setOnHardwareSkin (
                        juce::ImageFileFormat::loadFrom (apiBtn[apiSlot].off, (size_t) apiBtnSz[apiSlot][0]),
                        juce::ImageFileFormat::loadFrom (apiBtn[apiSlot].on,  (size_t) apiBtnSz[apiSlot][1]));
                }
                ++apiSlot;
            }
            else
            {
                s->setBandTitles (Fusion::BAND_NAMES()[(size_t) s->index],
                                  Fusion::BAND_NAMES_CN()[(size_t) s->index]
                                      + Fusion::U8 (" \u00b7 ") + juce::String (s->index + 1));
                s->setTextureStyle (tube ? 4                           // 温暖=Manley 黑钮
                                        : -1);                         // 其它=矢量银钮
            }

            ++sslSlot;
        }

        s->setAccentColour (accent);   // 旋钮环 / LED 跟随模式强调色
    }

    resized();     // 4 段时机架重排（条带自动加宽）

    for (auto* b : modeButtons)
    {
        b->setColour (juce::TextButton::buttonOnColourId, accent);
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        b->setToggleState (b->modeIndex == (int) m, juce::dontSendNotification);
    }

    modeTag.setText (spec.tag, juce::dontSendNotification);
    modeTag.setColour (juce::Label::backgroundColourId, accent);

    /* 通透模式 →「S-EQ」艺术字标签，Pultec 模式 →「P-EQ」，其余模式保持原模式标签 */
    /* 六个模式统一走艺术字徽章（与 S-EQ / P-EQ 同款细宽+辉光），原中文标签退役 */
    static const juce::String badgeFor[] = {
        "EQ-6", "Tube EQ", "P-EQ", "S-EQ", "N-EQ", "A-EQ"
    };
    const int badgeIdx = juce::jlimit (0, (int) (sizeof (badgeFor) / sizeof (badgeFor[0])) - 1, (int) m);

    modeTag.setVisible (false);
    seqBadge.setVisible (true);
    seqBadge.setAccent (accent);
    seqBadge.setText (badgeFor[badgeIdx]);

    lastMode = m;
    repaint();
}

/* ============================ 机架 ============================ */
void FusionEQAudioProcessorEditor::buildStrips()
{
    hpfStrip.onButton.setClickingTogglesState (true);
    hpfAttach = std::make_unique<BtnAttach> (processor.apvts, IDs::hpfOn, hpfStrip.onButton);
    hpfStrip.lpButton.setClickingTogglesState (true);
    lpAttach  = std::make_unique<BtnAttach> (processor.apvts, IDs::lpOn, hpfStrip.lpButton);
    canvas.addAndMakeVisible (hpfStrip);

    for (int i = 0; i < Fusion::numBands; ++i)
    {
        auto* s = bandStrips.add (new Fusion::BandStrip (
            processor, i, bandNames[(size_t) i], Fusion::BAND_NAMES_CN()[(size_t) i]));
        bandAttach.push_back (std::make_unique<BtnAttach> (
            processor.apvts, IDs::bOn (i), s->onButton));
        s->onHover = [this] (int band) { spectrum.setHighlightBand (band); };
        canvas.addAndMakeVisible (s);
    }

    airStrip.onButton.setClickingTogglesState (true);
    airAttach = std::make_unique<BtnAttach> (processor.apvts, IDs::airOn, airStrip.onButton);
    canvas.addAndMakeVisible (airStrip);

    canvas.addAndMakeVisible (plLowStrip);
    canvas.addAndMakeVisible (plHighStrip);
    canvas.addAndMakeVisible (plAttenStrip);
    plLowStrip.setVisible   (false);   // 初始由 refreshMode 按模式显隐
    plHighStrip.setVisible  (false);
    plAttenStrip.setVisible (false);
}

/* 宿主自动化改动了开关后，同步按钮文字 */
void FusionEQAudioProcessorEditor::syncToggleText()
{
    auto sync = [] (juce::TextButton& b)
    {
        const auto t = b.getToggleState() ? "ON" : "OFF";
        if (b.getButtonText() != juce::String (t))
            b.setButtonText (t);
    };

    sync (hpfStrip.onButton);
    sync (hpfStrip.lpButton);
    sync (airStrip.onButton);

    for (auto* s : bandStrips)
    {
        sync (s->onButton);
        s->updateTypeLabel();
        s->updateCurveState();     // SSL LF 曲线按钮亮灯跟随类型参数
    }
}

/* ============================ 绘制 ============================ */
void FusionEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    /* 背景绘制也走逻辑坐标系：与画布同一缩放（窗口物理尺寸 = 逻辑 × uiScale） */
    g.addTransform (juce::AffineTransform::scale (uiScale));

    const auto r = juce::Rectangle<float> (unitWidth, unitHeight);
    const bool sslSkin = processor.getMode() == Fusion::Mode::SSL;
    const bool plSkinOn = processor.getMode() == Fusion::Mode::Pultec;
    const bool nvSkin = processor.getMode() == Fusion::Mode::Neve;
    const bool apSkin = processor.getMode() == Fusion::Mode::API;
    const bool mpSkin = processor.getMode() == Fusion::Mode::Manley;

    /* 硬件深色外观：5 种硬件模式整机转深，配色按模式索引取表。
       层次保持 外壳(顶/底栏) → 机箱 → 模块面板：外壳比机箱浅一档，
       模块面板最亮（SSL/Neve）或同为深黑（API/Manley）。 */
    const bool heavySkin = sslSkin || plSkinOn || nvSkin || apSkin || mpSkin;
    const int  ck = sslSkin ? 0 : nvSkin ? 1 : apSkin ? 2 : mpSkin ? 3 : 4;

    static const juce::uint32 chT[5] = { 0xff343a40, 0xff37424a, 0xff101214,
                                         0xff1b1d20, 0xff262b30 };
    static const juce::uint32 chB[5] = { 0xff21252a, 0xff232a30, 0xff07080a,
                                         0xff0c0d10, 0xff171a1e };
    /* 顶栏 / 标题条 / 底栏（软件外壳） */
    static const juce::uint32 barT[5] = { 0xff3f474e, 0xff44515b, 0xff1e2124,
                                          0xff27292d, 0xff323940 };
    static const juce::uint32 barB[5] = { 0xff2e343a, 0xff313b42, 0xff121416,
                                          0xff181a1d, 0xff252a2f };

    /* 面板底色（统一浅色渐变，所有模式一致） */
    {
        juce::ColourGradient face (cFace, 0.0f, 0.0f, cFace2, 0.0f, (float) unitHeight, false);
        g.setGradientFill (face);
        g.fillAll();
    }

    /* 模块卡片（iOS 分组：圆角 + 极淡渐变 + 柔和投影 + 顶部高光），
       靠"卡片"本身区分分组，比分隔线更克制、更高级。
       所有模式统一：温润 = 三 Pultec 模块 + AIR；其余 = HPF + 各频段 + AIR。 */
    {
        juce::Array<juce::Component*> cols;

        if (processor.getMode() == Fusion::Mode::Pultec && plLowStrip.isVisible())
        {
            cols.add (&plLowStrip);
            cols.add (&plHighStrip);
            cols.add (&plAttenStrip);
            cols.add (&airStrip);
        }
        else
        {
            if (hpfStrip.isVisible()) cols.add (&hpfStrip);

            for (auto* s : bandStrips)
                if (s->isVisible()) cols.add (s);

            cols.add (&airStrip);
        }

        const float radius = heavySkin ? 8.0f : 16.0f;

        /* --- 硬件模式机箱框架 ---
           深色硬件卡片不再直接浮在浅色桌面上：先在卡片群外圈画一台
           同色系（比面板更深一档）的机箱，卡片像插入机架的模块。
           窗口顶栏/底栏是软件外壳（比机箱浅一档），层次：外壳 → 机箱 → 模块面板。 */
        if (heavySkin)
        {
            juce::Rectangle<float> cardsUnion;

            for (auto* c : cols)
                if (! c->getBounds().isEmpty())
                    cardsUnion = cardsUnion.isEmpty() ? c->getBounds().toFloat()
                                                      : cardsUnion.getUnion (c->getBounds().toFloat());

            if (! cardsUnion.isEmpty())
            {
                const auto ch = cardsUnion.expanded (12.0f, 8.0f);

                const juce::DropShadow chSh (juce::Colour (0x33000000), 18,
                                             juce::Point<int> (0, 4));
                chSh.drawForRectangle (g, ch.toNearestInt());

                juce::ColourGradient chg (juce::Colour (chT[(size_t) ck]), ch.getX(), ch.getY(),
                                          juce::Colour (chB[(size_t) ck]), ch.getX(),
                                          ch.getBottom(), false);
                g.setGradientFill (chg);
                g.fillRoundedRectangle (ch, 10.0f);

                /* 机箱边框：外侧黑描边 + 顶缘一线金属高光 + 底缘落影线 */
                g.setColour (juce::Colour (0x8c000000));
                g.drawRoundedRectangle (ch, 10.0f, 1.2f);
                g.setColour (juce::Colour (0x2effffff));
                g.drawHorizontalLine ((int) ch.getY() + 1, ch.getX() + 8.0f, ch.getRight() - 8.0f);
                g.setColour (juce::Colour (0x4d000000));
                g.drawHorizontalLine ((int) ch.getBottom() - 1, ch.getX() + 6.0f, ch.getRight() - 6.0f);
            }
        }

        for (auto* c : cols)
        {
            if (c->getBounds().isEmpty())
                continue;

            const auto card = c->getBounds().toFloat().reduced (2.0f, 1.0f);
            /* 柔和投影（大半径、低透明度，模拟环境光） */
            const juce::DropShadow sh (juce::Colour (0x1a000000), 14, juce::Point<int> (0, 3));
            sh.drawForRectangle (g, card.toNearestInt());

            if (heavySkin)
            {
                /* --- 硬件模块面板 ---
                   SSL 4000E：灰蓝金属；Pultec EQP-1A：更沉的石墨哑光；
                   Neve 1073：经典蓝灰面板（比 SSL 略蓝、略亮）；
                   API 550L：真机黑色面板（哑光黑，顶部一线金属高光）；
                   Manley：蓝灰机身 + 每段黑色插入面板（照 Massive Passive 原机）。
                   共用同一套绘制：金属渐变 + 顶部冷光高光 + 四角螺丝 */
                const juce::Colour cTop = sslSkin ? juce::Colour (0xff5a6167)
                                                  : nvSkin ? juce::Colour (0xff606d76)
                                                  : apSkin ? juce::Colour (0xff3c3e41)
                                                  : mpSkin ? juce::Colour (0xff2c2e31)
                                                           : juce::Colour (0xff4e5358);
                const juce::Colour cBot = sslSkin ? juce::Colour (0xff3b4046)
                                                  : nvSkin ? juce::Colour (0xff414c53)
                                                  : apSkin ? juce::Colour (0xff131416)
                                                  : mpSkin ? juce::Colour (0xff0e0f11)
                                                           : juce::Colour (0xff2a2e33);

                juce::ColourGradient cg (cTop, card.getX(), card.getY(),
                                         cBot, card.getX(), card.getBottom(), false);
                g.setGradientFill (cg);
                g.fillRoundedRectangle (card, radius);

                /* 内侧顶部 1px 冷高光 + 外侧深描边（金属厚度感） */
                g.setColour (juce::Colour (0x2effffff));
                g.drawRoundedRectangle (card.reduced (0.5f), radius - 0.5f, 1.0f);
                g.setColour (juce::Colour (0x73000000));
                g.drawRoundedRectangle (card, radius, 1.0f);

                /* 四角十字螺丝 */
                const float sr = 4.2f, inset = 12.0f;
                const juce::Point<float> screws[4] {
                    { card.getX() + inset,     card.getY() + inset },
                    { card.getRight() - inset, card.getY() + inset },
                    { card.getX() + inset,     card.getBottom() - inset },
                    { card.getRight() - inset, card.getBottom() - inset }
                };

                for (auto& s : screws)
                {
                    const auto cx = s.x, cy = s.y;

                    juce::ColourGradient sg (juce::Colour (0xffc3cad0), cx - 1.5f, cy - 1.5f,
                                             juce::Colour (0xff3f454b), cx + 2.0f, cy + 2.0f, true);
                    g.setGradientFill (sg);
                    g.fillEllipse (cx - sr, cy - sr, sr * 2.0f, sr * 2.0f);

                    g.setColour (juce::Colour (0x59000000));
                    g.drawEllipse (cx - sr, cy - sr, sr * 2.0f, sr * 2.0f, 0.9f);

                    /* 一字槽（随机角度感的固定 45°） */
                    g.setColour (juce::Colour (0xff262b30));
                    g.drawLine (cx - sr * 0.55f, cy - sr * 0.55f,
                                cx + sr * 0.55f, cy + sr * 0.55f, 1.4f);
                }
            }
            else
            {
                /* 卡片底色：顶部近白 → 底部极淡灰蓝 */
                juce::ColourGradient cg (juce::Colour (0xfffdfefe), card.getX(), card.getY(),
                                         juce::Colour (0xffeff3f7), card.getX(), card.getBottom(), false);
                g.setGradientFill (cg);
                g.fillRoundedRectangle (card, radius);

                /* 内侧 1px 高光 + 外侧极淡描边，做出细腻的厚度感 */
                g.setColour (juce::Colour (0x7affffff));
                g.drawRoundedRectangle (card.reduced (0.5f), radius - 0.5f, 1.0f);
                g.setColour (juce::Colour (0x12000000));
                g.drawRoundedRectangle (card, radius, 1.0f);
            }
        }
    }

    /* 顶栏（硬件模式：深色外壳，同机箱一套配色） */
    if (heavySkin)
    {
        juce::ColourGradient tb (juce::Colour (barT[(size_t) ck]), 0.0f, 0.0f,
                                 juce::Colour (barB[(size_t) ck]), 0.0f, (float) topBarH, false);
        g.setGradientFill (tb);
        g.fillRect (r.withHeight ((float) topBarH));
        g.setColour (juce::Colour (0x8c000000));
        g.drawHorizontalLine (topBarH - 1, r.getX(), r.getRight());
        g.setColour (juce::Colour (0x26ffffff));
        g.drawHorizontalLine (1, r.getX(), r.getRight());
    }
    else
    {
        juce::ColourGradient tb (juce::Colour (0xfffbfcfd), 0.0f, 0.0f,
                          juce::Colour (0xffeef1f4), 0.0f, (float) topBarH, false);
        g.setGradientFill (tb);
        g.fillRect (r.withHeight ((float) topBarH));
        g.setColour (cLineDk);
        g.drawHorizontalLine (topBarH - 1, r.getX(), r.getRight());
        g.setColour (juce::Colour (0x55ffffff));
        g.drawHorizontalLine (1, r.getX(), r.getRight());
    }

    /* Logo：极简超细艺术字（方案⑥）—— TextLayout 精确排版，双色一体 */
    {
        const float x0 = r.getX() + 20.0f;

        auto logoFont = juce::Font (juce::Font ("Segoe UI Light", 31.0f, juce::Font::plain))
                            .withExtraKerningFactor (0.16f);

        juce::AttributedString as;
        as.setJustification (juce::Justification::centredLeft);
        /* 硬件模式顶栏转深：字标转浅色刻字（EQ 保留品牌蓝，稍提亮） */
        as.append ("FUSION ", logoFont, heavySkin ? juce::Colour (0xffd5dbe1)
                                                  : juce::Colour (0xff3a444e));
        as.append ("EQ",     logoFont, heavySkin ? juce::Colour (0xff2fa8e0)
                                                 : juce::Colour (0xff0e9bd6));

        juce::TextLayout tl;
        tl.createLayout (as, 10000.0f);
        tl.draw (g, juce::Rectangle<float> (x0, 4.0f, tl.getWidth() + 4.0f, 44.0f));

        /* 副标题：固定适中字距 */
        auto subFont = juce::Font (juce::Font ("Segoe UI", 10.0f, juce::Font::plain))
                           .withExtraKerningFactor (0.24f);
        g.setFont (subFont);
        g.setColour (heavySkin ? juce::Colour (0xff87929c) : juce::Colour (0xff98a2ac));
        g.drawSingleLineText ("6-BAND CHARACTER EQUALIZER",
                              (int) x0 + 2, (int) (4.0f + 44.0f));
    }

    /* 模式说明条（标题徽章 + 收起箭头行） */
    const auto descY = (float) topBarH;

    if (heavySkin)
    {
        juce::ColourGradient db (juce::Colour (barB[(size_t) ck]), 0.0f, descY,
                              juce::Colour (barT[(size_t) ck]).darker (0.06f), 0.0f, descY + (float) descH, false);
        g.setGradientFill (db);
        g.fillRect (r.withTop (descY).withHeight ((float) descH));
        g.setColour (juce::Colour (0x8c000000));
        g.drawHorizontalLine (topBarH + descH - 1, r.getX(), r.getRight());
    }
    else
    {
        juce::ColourGradient db (juce::Colour (0xffe7ebef), 0.0f, descY,
                          juce::Colour (0xffdde2e8), 0.0f, descY + (float) descH, false);
        g.setGradientFill (db);
        g.fillRect (r.withTop (descY).withHeight ((float) descH));
        g.setColour (cLine);
        g.drawHorizontalLine (topBarH + descH - 1, r.getX(), r.getRight());
    }

    /* 频谱区底（高度随收起动画伸缩） */
    const auto scopeY = (float) (topBarH + descH);

    if (heavySkin)
    {
        juce::ColourGradient sb (juce::Colour (barB[(size_t) ck]), 0.0f, scopeY,
                            juce::Colour (chB[(size_t) ck]), 0.0f, scopeY + (float) (scopeNowH + 16), false);
        g.setGradientFill (sb);
        g.fillRect (r.withTop (scopeY).withHeight ((float) (scopeNowH + 16)));
        g.setColour (juce::Colour (0x8c000000));
        g.drawHorizontalLine (topBarH + descH + scopeNowH + 15, r.getX(), r.getRight());
    }
    else
    {
        juce::ColourGradient sb (juce::Colour (0xffe4e8ec), 0.0f, scopeY,
                        juce::Colour (0xffd9dee4), 0.0f, scopeY + (float) (scopeNowH + 16), false);
        g.setGradientFill (sb);
        g.fillRect (r.withTop (scopeY).withHeight ((float) (scopeNowH + 16)));
        g.setColour (cLineDk);
        g.drawHorizontalLine (topBarH + descH + scopeNowH + 15, r.getX(), r.getRight());
    }

    /* 底栏 */
    const auto footY = (float) (unitHeight - footH);

    if (heavySkin)
    {
        juce::ColourGradient fb (juce::Colour (barT[(size_t) ck]).darker (0.06f), 0.0f, footY,
                                 juce::Colour (barB[(size_t) ck]), 0.0f, (float) unitHeight, false);
        g.setGradientFill (fb);
        g.fillRect (r.withTop (footY));
        g.setColour (juce::Colour (0x8c000000));
        g.drawHorizontalLine (unitHeight - footH, r.getX(), r.getRight());
        g.setColour (juce::Colour (0x22ffffff));
        g.drawHorizontalLine ((int) footY + 1, r.getX(), r.getRight());
    }
    else
    {
        juce::ColourGradient fb (juce::Colour (0xffe9edf1), 0.0f, footY,
                             juce::Colour (0xffdfe4e9), 0.0f, (float) unitHeight, false);
        g.setGradientFill (fb);
        g.fillRect (r.withTop (footY));
        g.setColour (cLineDk);
        g.drawHorizontalLine (unitHeight - footH, r.getX(), r.getRight());
    }
}

/* ============================ 布局 ============================ */
void FusionEQAudioProcessorEditor::resized()
{
    /* 拖拽缩放：窗口物理尺寸 → 整体仿射缩放逻辑画布（1180×932）。
       等比约束下 sx==sy；控件仍按逻辑坐标布局（子组件挂在 canvas 上）。 */
    uiScale = getWidth() / (float) unitWidth;
    canvas.setBounds (0, 0, unitWidth, unitHeight);
    canvas.setTransform (juce::AffineTransform::scale (uiScale));

    auto r = juce::Rectangle<int> (unitWidth, unitHeight);   // 逻辑坐标系

    /* 顶栏 */
    auto top = r.removeFromTop (topBarH);
    /* logoSub 已改由 paint() 直接绘制（动态字距撑满），不再作为控件 */

    const int totalW = 6 * 110 + 5 * 7;
    auto modesArea = top.withSizeKeepingCentre (totalW, 46);
    for (auto* b : modeButtons)
    {
        b->setBounds (modesArea.removeFromLeft (110).reduced (3, 5));
        modesArea.removeFromLeft (4);
    }

    bypassButton.setBounds (top.getRight() - 116, top.getY() + 14, 98, 38);

    /* 模式标签 / S-EQ · P-EQ 艺术字（说明文字已移除，区域留白） */
    auto desc = r.removeFromTop (descH);
    modeTag.setBounds  (desc.getX() + 18, desc.getY() + 14, 156, 26);
    seqBadge.setBounds (modeTag.getBounds());
    scopeToggle.setBounds (desc.getCentreX() - 18, desc.getCentreY() - 13, 36, 26);

    /* 频谱：高度随收起/展开动画伸缩 */
    const float e = scopeAnim * scopeAnim * (3.0f - 2.0f * scopeAnim);
    const int   scopeNow = juce::roundToInt ((float) scopeH * e);

    r.removeFromTop (8);

    if (scopeNow > 2)
    {
        spectrum.setVisible (true);
        spectrum.setBounds (r.removeFromTop (scopeNow));
    }
    else
    {
        spectrum.setVisible (false);   // 完全收起：停掉频谱 60Hz 重绘
        r.removeFromTop (2);           // 留一条分隔线位
    }

    scopeNowH = scopeNow;

    /* 机架 */
    auto rack = r.removeFromTop (rackH);
    rack.reduce (18, 0);

    /* 可见条带流式排布：SSL 4 段 = 6 条（HPF + LF/LMF/HMF/HF + AIR，自动加宽） */
    juce::Array<juce::Component*> visible;
    if (hpfStrip.isVisible())
        visible.add (&hpfStrip);

    for (auto* s : bandStrips)
        if (s->isVisible())
            visible.add (s);

    if (plLowStrip.isVisible())
    {
        visible.add (&plLowStrip);
        visible.add (&plHighStrip);
        visible.add (&plAttenStrip);
    }

    visible.add (&airStrip);

    const int n      = visible.size();
    const int stripW = (rack.getWidth() - rackGap * (n - 1)) / n;

    for (int i = 0; i < n; ++i)
        visible.getReference (i)->setBounds (rack.getX() + i * (stripW + rackGap),
                                             rack.getY(), stripW, rack.getHeight());

    /* 宝石旁通灯：放在温润 ATTEN 板块的下部空白（板块控件集中在顶部）。
       尺寸约为旋钮的一半。 */
    if (bypassGem.isVisible())
    {
        auto zone = plAttenStrip.getBounds();
        zone.removeFromTop (178);
        zone.reduce (6, 20);
        bypassGem.setBounds (zone.withSizeKeepingCentre (62, 82));
    }

    /* 底栏：AUTO GAIN 开关 + OVERSAMPLE 倍率选择 + OUTPUT 旋钮 */
    auto foot = r.removeFromBottom (footH);
    foot.reduce (18, 0);

    autoGainButton.setBounds (foot.getX(), foot.getY() + 22, 110, 32);

    if (outKnob != nullptr)
        outKnob->setBounds (foot.getX() + 132, foot.getY() + 12, 74, 54);

    osButton.setBounds (foot.getX() + 226, foot.getY() + 22, 138, 32);
}
