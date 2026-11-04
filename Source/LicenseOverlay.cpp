/* =============================================================================
   Fusion EQ  ·  LicenseOverlay.cpp
============================================================================= */
#include "LicenseOverlay.h"
#include "PluginProcessor.h"

LicenseOverlay::LicenseOverlay (FusionEQAudioProcessor& p)
    : processor (p)
{
    setInterceptsMouseClicks (true, true);   // 默认拦截（锁定态）；激活后改为透传

    /* —— 锁定态 —— */
    titleLabel.setText ("Fusion EQ", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (34.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, juce::Colour (kText));

    subtitleLabel.setText (juce::String::fromUTF8 ("需要激活以继续使用"), juce::dontSendNotification);
    subtitleLabel.setFont (juce::Font (17.0f));
    subtitleLabel.setColour (juce::Label::textColourId, juce::Colour (kText2));

    machineLabel.setFont (juce::Font (16.0f));   // 默认无衬线 → Windows 解析到微软雅黑（含中文），避免 Consolas 缺中文字形
    machineLabel.setColour (juce::Label::textColourId, juce::Colour (kText2));

    codeEditor.setMultiLine (false);
    codeEditor.setReturnKeyStartsNewLine (false);
    codeEditor.setTextToShowWhenEmpty (juce::String::fromUTF8 ("请输入激活码"), juce::Colour (0xff5c666f));
    codeEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xfff4f6f8));
    codeEditor.setColour (juce::TextEditor::textColourId,        juce::Colour (0xff1d242b));
    codeEditor.setColour (juce::TextEditor::outlineColourId,     juce::Colour (0xff3a6fd8));
    codeEditor.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (0xff2f9e6a));
    codeEditor.setFont (juce::Font (19.0f));
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
    buyButton.setFont (juce::Font (16.0f), false);

    statusLabel.setFont (juce::Font (16.0f));
    statusLabel.setColour (juce::Label::textColourId, juce::Colour (kText2));
    statusLabel.setJustificationType (juce::Justification::centred);

    /* —— 已激活态 —— */
    activatedLabel.setText (juce::String::fromUTF8 ("已激活"), juce::dontSendNotification);
    activatedLabel.setFont (juce::Font (17.0f, juce::Font::bold));
    activatedLabel.setColour (juce::Label::textColourId, juce::Colour (kAccent));
    activatedLabel.setInterceptsMouseClicks (false, false);

    badgeMachineLabel.setFont (juce::Font (13.0f));   // 默认无衬线 → 含中文
    badgeMachineLabel.setColour (juce::Label::textColourId, juce::Colour (kText2));
    badgeMachineLabel.setInterceptsMouseClicks (false, false);

    logoutButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3a3f46));
    logoutButton.setColour (juce::TextButton::textColourOffId, juce::Colour (kText));
    logoutButton.onClick = [this]()
    {
        taiji::LicenseManager::logout();
        refreshState();   // 重新锁定 + 关闭门控
    };

    addAndMakeVisible (titleLabel);
    addAndMakeVisible (subtitleLabel);
    addAndMakeVisible (machineLabel);
    addAndMakeVisible (codeEditor);
    addAndMakeVisible (activateButton);
    addAndMakeVisible (buyButton);
    addAndMakeVisible (statusLabel);

    addAndMakeVisible (activatedLabel);
    addAndMakeVisible (badgeMachineLabel);
    addAndMakeVisible (logoutButton);

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

    // 锁定态拦截全部鼠标；已激活态只让徽标里的「退出登录」按钮接收点击，
    // 其余区域透传给插件（setInterceptsMouseClicks(false, true)）。
    setInterceptsMouseClicks (! act, true);

    titleLabel.setVisible      (! act);
    subtitleLabel.setVisible   (! act);
    machineLabel.setVisible    (! act);
    codeEditor.setVisible      (! act);
    activateButton.setVisible  (! act);
    buyButton.setVisible       (! act);
    statusLabel.setVisible     (! act);

    activatedLabel.setVisible    (act);
    badgeMachineLabel.setVisible (act);
    logoutButton.setVisible      (act);

    if (act)
    {
        const auto mid = st["machineId"].toString();
        badgeMachineLabel.setText (juce::String::fromUTF8 ("机器码：") + mid, juce::dontSendNotification);
        statusLabel.setText ({}, juce::dontSendNotification);
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

    // 已激活：仅在角落画一张小徽标卡片（不拦截鼠标，插件照常可用）
    if (badgeRect.getWidth() <= 0.0f)
        return;

    g.setColour (juce::Colour (0xdd232a31));
    g.fillRoundedRectangle (badgeRect, 10.0f);
    g.setColour (juce::Colour (kAccent));
    g.drawRoundedRectangle (badgeRect, 10.0f, 1.0f);
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

    // —— 已激活徽标（右上角 desc 区，避开顶栏按钮与频谱）——
    const int bw = 380, bh = 56;
    badgeRect = juce::Rectangle<float> ((float) (b.getWidth() - bw - 16),
                                        (float) (66 + 6),   // topBarH(66) 之下
                                        (float) bw, (float) bh);

    activatedLabel.setBounds    (b.getWidth() - bw - 16 + 16, 66 + 9,  64, 18);
    badgeMachineLabel.setBounds (b.getWidth() - bw - 16 + 16, 66 + 30, bw - 16 - 76, 18);
    logoutButton.setBounds      (b.getWidth() - 16 - 70,        66 + 13, 70, 30);
}
