/* =============================================================================
   Fusion EQ  ·  LicenseOverlay.cpp
============================================================================= */
#include "LicenseOverlay.h"
#include "PluginProcessor.h"

/* 缩放档位标签（75% / 100% / 125% / 150%），与编辑器侧 uiScales 一一对应 */
const char* LicenseOverlay::scaleLabels[LicenseOverlay::kScaleCount] =
    { "75%", "100%", "125%", "150%" };

/* ============================ 设置面板 ============================ */
LicenseOverlay::SettingsPanel::SettingsPanel (FusionEQAudioProcessor& p, LicenseOverlay& owner)
    : processor (p), owner (owner)
{
    titleLabel.setText (juce::String::fromUTF8 ("设置"), juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::String ("Microsoft YaHei"), 20.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, juce::Colour (kText));

    closeButton.setButtonText (juce::String::fromUTF8 ("×"));   // × 关闭
    closeButton.setColour (juce::TextButton::buttonColourId,    juce::Colours::transparentBlack);
    closeButton.setColour (juce::TextButton::buttonOnColourId,  juce::Colours::transparentBlack);
    closeButton.setColour (juce::TextButton::textColourOffId,   juce::Colour (kText2));
    closeButton.setColour (juce::TextButton::textColourOnId,    juce::Colour (kText));
    closeButton.onClick = [this, &owner]() { owner.closeSettings(); };

    sectionLabel.setText (juce::String::fromUTF8 ("授权"), juce::dontSendNotification);
    sectionLabel.setFont (juce::Font (juce::String ("Microsoft YaHei"), 12.0f, juce::Font::bold));
    sectionLabel.setColour (juce::Label::textColourId, juce::Colour (kAccent));

    statusLabel.setFont (juce::Font (juce::String ("Microsoft YaHei"), 18.0f, juce::Font::bold));
    statusLabel.setColour (juce::Label::textColourId, juce::Colour (kAccent));

    machineLabel.setFont (juce::Font (juce::String ("Microsoft YaHei"), 14.0f, juce::Font::plain));   // 显式雅黑：确保「机器码：」汉字与 hash 同字体正确渲染
    machineLabel.setColour (juce::Label::textColourId, juce::Colour (kText2));

    copyButton.setButtonText (juce::String::fromUTF8 ("复制"));
    copyButton.setColour (juce::TextButton::buttonColourId,    juce::Colour (0xff2a3038));
    copyButton.setColour (juce::TextButton::textColourOffId,   juce::Colour (kText2));
    copyButton.setColour (juce::TextButton::textColourOnId,    juce::Colour (kText));
    copyButton.getProperties().set ("fontH", 14);
    copyButton.onClick = [this]()
    {
        const auto mid = taiji::LicenseManager::getState()["machineId"].toString();
        juce::SystemClipboard::copyTextToClipboard (mid);
        copyButton.setButtonText (juce::String::fromUTF8 ("已复制"));
        juce::Component::SafePointer<SettingsPanel> safe (this);
        juce::Timer::callAfterDelay (1200, [safe]
        {
            if (safe != nullptr)
                safe->copyButton.setButtonText (juce::String::fromUTF8 ("复制"));
        });
    };

    logoutButton.setButtonText (juce::String::fromUTF8 ("退出登录"));
    logoutButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3f46));
    logoutButton.setColour (juce::TextButton::textColourOffId,  juce::Colour (kText));
    logoutButton.setColour (juce::TextButton::textColourOnId,   juce::Colour (kText));
    logoutButton.getProperties().set ("fontH", 18);
    logoutButton.onClick = [this, &owner]() { owner.doLogout(); };

    siteButton.setButtonText (juce::String::fromUTF8 ("前往官网购买 / 认领激活码"));
    siteButton.setURL (juce::URL ("https://ohaudio.cn/account/"));
    siteButton.setColour (juce::HyperlinkButton::textColourId, juce::Colour (0xff6fb3ff));
    siteButton.setFont (juce::Font (juce::String ("Microsoft YaHei"), 14.0f, juce::Font::plain), false);   // 显式雅黑：链接含中文「前往官网购买 / 认领激活码」，否则中文不渲染只剩「/」

    /* 界面缩放：75% / 100% / 125% / 150%，点选回调到编辑器 applyUiScale */
    zoomSectionLabel.setText (juce::String::fromUTF8 ("界面缩放"), juce::dontSendNotification);
    zoomSectionLabel.setFont (juce::Font (juce::String ("Microsoft YaHei"), 12.0f, juce::Font::bold));
    zoomSectionLabel.setColour (juce::Label::textColourId, juce::Colour (kAccent));

    for (int i = 0; i < LicenseOverlay::kScaleCount; ++i)
    {
        auto* b = zoomButtons.add (new juce::TextButton (
                        juce::String::fromUTF8 (LicenseOverlay::scaleLabels[i])));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (0x5a3117);                       // 互斥高亮
        b->setColour (juce::TextButton::buttonColourId,    juce::Colour (0xff2a3038));
        b->setColour (juce::TextButton::buttonOnColourId,  juce::Colour (kAccent));
        b->setColour (juce::TextButton::textColourOffId,   juce::Colour (kText2));
        b->setColour (juce::TextButton::textColourOnId,    juce::Colours::white);
        b->getProperties().set ("fontH", 13);
        b->onClick = [this, &owner, i]()
        {
            if (owner.onScalePick) owner.onScalePick (i);
            setActiveZoom (i);
        };
        addAndMakeVisible (b);
    }

    addAndMakeVisible (titleLabel);
    addAndMakeVisible (closeButton);
    addAndMakeVisible (sectionLabel);
    addAndMakeVisible (statusLabel);
    addAndMakeVisible (machineLabel);
    addAndMakeVisible (copyButton);
    addAndMakeVisible (zoomSectionLabel);
    addAndMakeVisible (logoutButton);
    addAndMakeVisible (siteButton);

    setInterceptsMouseClicks (true, true);   // 面板自身拦截，点击卡片外不穿透到插件
}

void LicenseOverlay::SettingsPanel::refresh()
{
    auto st = taiji::LicenseManager::getState();
    const bool act = (bool) st["activated"];

    statusLabel.setColour (juce::Label::textColourId,
                           act ? juce::Colour (kAccent) : juce::Colour (kWarn));
    statusLabel.setText (act ? juce::String::fromUTF8 ("已激活")
                             : juce::String::fromUTF8 ("未激活"),
                         juce::dontSendNotification);

    const auto mid = st["machineId"].toString();
    machineLabel.setText (juce::String::fromUTF8 ("机器码：") + mid, juce::dontSendNotification);

    logoutButton.setVisible (act);   // 未激活时隐藏退出登录（无可退）
    copyButton.setVisible (true);

    // 高亮当前缩放档（编辑器按实际 uiScale 反查）
    if (owner.getScaleIndex)
        setActiveZoom (owner.getScaleIndex());
}

void LicenseOverlay::SettingsPanel::setActiveZoom (int idx)
{
    for (int i = 0; i < zoomButtons.size(); ++i)
        zoomButtons[i]->setToggleState (i == idx, juce::dontSendNotification);
}

void LicenseOverlay::SettingsPanel::paint (juce::Graphics& g)
{
    // 半透明深色遮罩，压住后面的插件 UI（设置面板为模态）
    g.fillAll (juce::Colour (0xaa0b0d10));

    if (cardRect.getWidth() <= 0.0f)
        return;

    // 卡片投影 + 底色
    const juce::DropShadow sh (juce::Colour (0x55000000), 24, juce::Point<int> (0, 6));
    sh.drawForRectangle (g, cardRect.toNearestIntEdges());

    g.setColour (juce::Colour (kCard));
    g.fillRoundedRectangle (cardRect, 14.0f);

    // 卡片顶部强调条
    g.setColour (juce::Colour (kAccent));
    g.fillRoundedRectangle (cardRect.getX(), cardRect.getY(), cardRect.getWidth(), 5.0f, 2.0f);
}

void LicenseOverlay::SettingsPanel::resized()
{
    cardRect = getLocalBounds().toFloat().withSizeKeepingCentre (380.0f, 350.0f);
    const float x  = cardRect.getX() + 28.0f;
    const float w  = cardRect.getWidth() - 56.0f;
    const float cy = cardRect.getY();

    titleLabel.setBounds    (x,                       cy + 22, w - 30, 28);
    closeButton.setBounds   (cardRect.getRight() - 28 - 24, cy + 18, 26, 26);

    sectionLabel.setBounds  (x,                       cy + 64,  w, 20);
    statusLabel.setBounds   (x,                       cy + 88,  w, 26);

    machineLabel.setBounds  (x,                       cy + 124, w - 70, 22);
    copyButton.setBounds    (cardRect.getRight() - 28 - 58, cy + 122, 58, 26);

    // 界面缩放：分组标题 + 一排 4 档按钮
    zoomSectionLabel.setBounds (x,                     cy + 164, w, 20);
    const float zy   = cy + 188.0f;
    const float zgap = 8.0f;
    const float zw   = (w - 3.0f * zgap) / 4.0f;
    for (int i = 0; i < zoomButtons.size(); ++i)
        zoomButtons[i]->setBounds ((int) (x + i * (zw + zgap)), (int) zy, (int) zw, 36);

    const float by = cy + 250;
    const float bw = (w - 12.0f) / 2.0f;
    logoutButton.setBounds (x,                 by, bw, 44);
    siteButton.setBounds   (x + bw + 12.0f,    by, bw, 44);
}

/* ============================ 覆盖层 ============================ */
LicenseOverlay::LicenseOverlay (FusionEQAudioProcessor& p)
    : processor (p)
{
    setInterceptsMouseClicks (true, true);   // 默认拦截（锁定态）；激活且未开设置时改为透传

    /* —— 锁定态 —— */
    titleLabel.setText ("Fusion EQ", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::String ("Microsoft YaHei"), 34.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, juce::Colour (kText));

    subtitleLabel.setText (juce::String::fromUTF8 ("需要激活以继续使用"), juce::dontSendNotification);
    subtitleLabel.setFont (juce::Font (juce::String ("Microsoft YaHei"), 17.0f, juce::Font::plain));
    subtitleLabel.setColour (juce::Label::textColourId, juce::Colour (kText2));

    machineLabel.setFont (juce::Font (juce::String ("Microsoft YaHei"), 16.0f, juce::Font::plain));   // 显式雅黑：确保「机器码」汉字与 hash 同字体正确渲染
    machineLabel.setColour (juce::Label::textColourId, juce::Colour (kText2));

    codeEditor.setMultiLine (false);
    codeEditor.setReturnKeyStartsNewLine (false);
    codeEditor.setTextToShowWhenEmpty (juce::String::fromUTF8 ("请输入激活码"), juce::Colour (0xff5c666f));
    codeEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xfff4f6f8));
    codeEditor.setColour (juce::TextEditor::textColourId,        juce::Colour (0xff1d242b));
    codeEditor.setColour (juce::TextEditor::outlineColourId,     juce::Colour (0xff3a6fd8));
    codeEditor.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (0xff2f9e6a));
    codeEditor.setFont (juce::Font (juce::String ("Microsoft YaHei"), 19.0f, juce::Font::plain));
    // 回车即激活
    codeEditor.onReturnKey = [this]() { doActivate (codeEditor.getText()); };

    activateButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (kAccent));
    activateButton.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
    activateButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
    activateButton.getProperties().set ("fontH", 22);   // 通过 FusionLook 的 fontH 属性放大按钮字号
    activateButton.onClick = [this]() { doActivate (codeEditor.getText()); };

    buyButton.setButtonText (juce::String::fromUTF8 ("前往官网购买 / 认领激活码"));
    buyButton.setURL (juce::URL ("https://ohaudio.cn/account/"));
    buyButton.setColour (juce::HyperlinkButton::textColourId, juce::Colour (0xff6fb3ff));
    buyButton.setFont (juce::Font (juce::String ("Microsoft YaHei"), 16.0f, juce::Font::plain), false);   // 显式雅黑：锁定态官网链接含中文，否则中文不渲染只剩「/」

    statusLabel.setFont (juce::Font (juce::String ("Microsoft YaHei"), 16.0f, juce::Font::plain));
    statusLabel.setColour (juce::Label::textColourId, juce::Colour (kText2));
    statusLabel.setJustificationType (juce::Justification::centred);

    /* —— 已激活态：低调齿轮入口（透明按钮，齿轮由 paint 绘制）—— */
    gearButton.setButtonText ({});
    gearButton.setColour (juce::TextButton::buttonColourId,    juce::Colours::transparentBlack);
    gearButton.setColour (juce::TextButton::buttonOnColourId,  juce::Colours::transparentBlack);
    gearButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    gearButton.onClick = [this]() { openSettings(); };
    gearButton.addMouseListener (this, false);   // 悬停重绘由 mouseEnter/Exit 触发
    gearButton.setVisible (false);

    /* —— 设置面板 —— */
    settingsPanel = std::make_unique<SettingsPanel> (processor, *this);
    settingsPanel->setVisible (false);

    addAndMakeVisible (titleLabel);
    addAndMakeVisible (subtitleLabel);
    addAndMakeVisible (machineLabel);
    addAndMakeVisible (codeEditor);
    addAndMakeVisible (activateButton);
    addAndMakeVisible (buyButton);
    addAndMakeVisible (statusLabel);

    addAndMakeVisible (gearButton);
    addAndMakeVisible (*settingsPanel);

    refreshState();
}

LicenseOverlay::~LicenseOverlay() = default;

void LicenseOverlay::refreshState()
{
    auto st = taiji::LicenseManager::getState();
    const bool act = (bool) st["activated"];
    active = act;

    // 授权门与界面同步：已激活 → 放行音频；否则整条链静音。
    processor.setLicensed (act);

    // 锁定态拦截全部鼠标；已激活且未开设置 → 透传（setInterceptsMouseClicks(false, true)）；
    // 设置展开 → 拦截（模态）。
    setInterceptsMouseClicks (! act || settingsOpen, true);

    titleLabel.setVisible      (! act);
    subtitleLabel.setVisible   (! act);
    machineLabel.setVisible    (! act);
    codeEditor.setVisible      (! act);
    activateButton.setVisible  (! act);
    buyButton.setVisible       (! act);
    statusLabel.setVisible     (! act);

    gearButton.setVisible      (act && ! settingsOpen);
    settingsPanel->setVisible  (settingsOpen);

    if (act)
    {
        settingsPanel->refresh();   // 保持面板内容最新
    }
    else
    {
        const auto mid = st["machineId"].toString();
        machineLabel.setText (juce::String::fromUTF8 ("机器码（绑定本机）：") + mid, juce::dontSendNotification);
        statusLabel.setText ({}, juce::dontSendNotification);
        codeEditor.setText ({}, juce::dontSendNotification);
    }

    repaint();
}

void LicenseOverlay::openSettings()
{
    if (! active)
        return;

    settingsOpen = true;
    gearButton.setVisible (false);
    settingsPanel->setVisible (true);
    settingsPanel->refresh();
    setInterceptsMouseClicks (true, true);   // 模态：拦截所有点击
    repaint();
}

void LicenseOverlay::closeSettings()
{
    settingsOpen = false;
    settingsPanel->setVisible (false);
    gearButton.setVisible (active);
    setInterceptsMouseClicks (! active, true);   // 恢复：激活→透传，未激活→拦截
    repaint();
}

void LicenseOverlay::doLogout()
{
    taiji::LicenseManager::logout();
    closeSettings();    // 先收面板，再刷新（刷新会据新状态重新锁定）
    refreshState();     // 重新锁定 + 关闭门控
}

void LicenseOverlay::startLaunchVerify()
{
    if (! taiji::LicenseManager::shouldVerifyOnLaunch())
        return;

    opIsActivate = false;   // 这是启动回验，不是手动激活

    juce::Component::SafePointer<LicenseOverlay> safe (this);
    std::thread ([safe]
    {
        juce::var res;
        try { res = taiji::LicenseManager::verify(); }
        catch (...) { res = makeNetError(); }
        juce::MessageManager::callAsync ([safe, res]
        {
            if (safe != nullptr)
                safe->onResult (res);
        });
    }).detach();
}

void LicenseOverlay::doActivate (const juce::String& code)
{
    if (busy)
        return;

    if (code.trim().isEmpty())
    {
        statusLabel.setText (juce::String::fromUTF8 ("请输入激活码"), juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, juce::Colour (kWarn));
        return;
    }

    busy = true;
    opIsActivate = true;   // 手动激活操作
    activateButton.setEnabled (false);
    statusLabel.setColour (juce::Label::textColourId, juce::Colour (kText2));
    statusLabel.setText (juce::String::fromUTF8 ("正在激活…"), juce::dontSendNotification);

    juce::Component::SafePointer<LicenseOverlay> safe (this);
    const auto dev = juce::SystemStats::getComputerName();
    const auto osn = juce::SystemStats::getOperatingSystemName();
    const auto ver = taiji::LicenseManager::version();
    const juce::String plugin = taiji::product::id();   // = "FusionEQ"

    std::thread ([safe, code, dev, osn, plugin, ver]
    {
        juce::var res;
        try { res = taiji::LicenseManager::activate (code, dev, osn, plugin, ver); }
        catch (...) { res = makeNetError(); }
        juce::MessageManager::callAsync ([safe, res]
        {
            if (safe != nullptr)
                safe->onResult (res);
        });
    }).detach();
}

juce::var LicenseOverlay::makeNetError()
{
    juce::var e (new juce::DynamicObject());
    e.getDynamicObject()->setProperty ("ok", false);
    e.getDynamicObject()->setProperty ("code", "NETWORK");
    e.getDynamicObject()->setProperty ("error", juce::String::fromUTF8 ("网络请求异常，请检查网络或稍后重试"));
    return e;
}

void LicenseOverlay::onResult (const juce::var& res)
{
    busy = false;
    activateButton.setEnabled (true);

    const bool ok   = (bool) res["ok"];
    const auto code = res["code"].toString();
    const auto err  = res["error"].toString();

    if (ok)
    {
        statusLabel.setColour (juce::Label::textColourId, juce::Colour (kAccent));
        statusLabel.setText (juce::String::fromUTF8 ("激活成功！"), juce::dontSendNotification);
        refreshState();   // 重新读凭证 → 解锁界面 + 打开门控
        return;
    }

    juce::String msg;
    if      (code == "INVALID_CODE" || code == "CODE_INVALID")
        msg = juce::String::fromUTF8 ("激活码无效，请检查后重试");
    else if (code == "DEVICE_LIMIT")
        msg = juce::String::fromUTF8 ("激活码已达设备上限，请前往官网解绑旧设备");
    else if (code == "PRODUCT_MISMATCH")
        msg = juce::String::fromUTF8 ("该激活码不适用于 Fusion EQ");
    else if (code == "NETWORK")
        msg = juce::String::fromUTF8 ("网络连接失败，请检查网络后重试");
    else if (code == "PARSE" || code == "NO_LICENSE")
        msg = juce::String::fromUTF8 ("服务器返回异常，请稍后重试");
    else
        msg = err.isNotEmpty() ? err : juce::String::fromUTF8 ("激活失败，请稍后重试");

    statusLabel.setColour (juce::Label::textColourId, juce::Colour (kWarn));
    statusLabel.setText (msg, juce::dontSendNotification);
    statusLabel.repaint();

    // 关键修复：手动激活失败时不要调用 refreshState()——它会把 statusLabel 清空，
    // 导致「错误提示一闪即没、看起来像点了没反应」。仅当这是「启动回验」需要翻回锁定态时才刷新。
    if (! opIsActivate)
    {
        refreshState();
        statusLabel.setText (msg, juce::dontSendNotification);   // 刷新后再补回错误文案
        statusLabel.repaint();
    }
}

void LicenseOverlay::paint (juce::Graphics& g)
{
    if (! active)
    {
        // 半透明深色背景，挡住后面的插件 UI（覆盖层不可关闭）
        g.fillAll (juce::Colour (kBackdrop));

        // 居中卡片
        auto card = getLocalBounds().toFloat().withSizeKeepingCentre (540.0f, 452.0f);
        g.setColour (juce::Colour (0x40000000));
        g.fillRoundedRectangle (card.expanded (6.0f, 6.0f), 18.0f);
        g.setColour (juce::Colour (kCard));
        g.fillRoundedRectangle (card, 14.0f);

        // 卡片顶部强调条
        g.setColour (juce::Colour (kAccent));
        g.fillRoundedRectangle (card.getX(), card.getY(), card.getWidth(), 5.0f, 2.0f);
        return;
    }

    // 已激活且未开设置：仅在右上角画一颗低调的齿轮（点击由 gearButton 捕获）
    if (! settingsOpen && gearRect.getWidth() > 0.0f)
        drawCog (g, gearRect, gearButton.isOver());
}

void LicenseOverlay::mouseEnter (const juce::MouseEvent&) { repaint(); }
void LicenseOverlay::mouseExit  (const juce::MouseEvent&) { repaint(); }

void LicenseOverlay::drawCog (juce::Graphics& g, juce::Rectangle<float> r, bool hover)
{
    const float cx = r.getCentreX();
    const float cy = r.getCentreY();
    const float R  = r.getWidth() * 0.46f;       // 外缘（齿尖）
    const float ri = R * 0.60f;                  // 轮体半径
    const int   teeth = 8;

    const juce::Colour cog = hover ? juce::Colour (0xff3a444e)
                                   : juce::Colour (0xff8b949c);   // 浅色顶栏/深色硬件栏都看得见

    // 齿：8 个三角形
    for (int i = 0; i < teeth; ++i)
    {
        const float a  = (float) i * juce::MathConstants<float>::twoPi / (float) teeth;
        const float tx = cx + std::cos (a) * R;
        const float ty = cy + std::sin (a) * R;
        const float t2x = cx + std::cos (a + 0.20f) * (R * 0.74f);
        const float t2y = cy + std::sin (a + 0.20f) * (R * 0.74f);
        const float t3x = cx + std::cos (a - 0.20f) * (R * 0.74f);
        const float t3y = cy + std::sin (a - 0.20f) * (R * 0.74f);
        juce::Path p;
        p.addTriangle (tx, ty, t2x, t2y, t3x, t3y);
        g.setColour (cog);
        g.fillPath (p);
    }

    // 轮体
    g.setColour (cog);
    g.fillEllipse (cx - ri, cy - ri, 2.0f * ri, 2.0f * ri);

    // 中心轴孔（描边，营造齿轮观感）
    g.setColour (hover ? juce::Colour (0xffe8ecf0) : juce::Colour (0xff5c666f));
    g.drawEllipse (cx - ri * 0.5f, cy - ri * 0.5f, ri, ri, 2.0f);
}

void LicenseOverlay::resized()
{
    auto b = getLocalBounds();

    // —— 锁定态卡片内控件布局 ——
    auto card = b.withSizeKeepingCentre (540, 452);
    const int x = card.getX() + 50;
    const int w = card.getWidth() - 100;

    titleLabel.setBounds        (x, card.getY() + 36,  w, 40);
    subtitleLabel.setBounds     (x, card.getY() + 86,  w, 26);
    machineLabel.setBounds      (x, card.getY() + 126, w, 28);
    codeEditor.setBounds        (x, card.getY() + 166, w, 52);
    activateButton.setBounds    (x, card.getY() + 238, w, 54);
    buyButton.setBounds         (x, card.getY() + 312, w, 30);
    statusLabel.setBounds       (x, card.getY() + 354, w, 70);

    // —— 已激活齿轮（说明条右上角，避开顶栏按钮与频谱）—— 
    const int gs = 28;
    gearRect = juce::Rectangle<float> ((float) (b.getWidth() - 16 - gs),
                                       (float) (66 + (54 - gs) / 2),   // topBarH(66) + desc 居中
                                       (float) gs, (float) gs);
    gearButton.setBounds (juce::Rectangle<int> ((int) gearRect.getX(), (int) gearRect.getY(),
                                                (int) gearRect.getWidth(), (int) gearRect.getHeight()));

    // 设置面板铺满整块画布
    settingsPanel->setBounds (b);
}
