/* =============================================================================
   Fusion EQ  ·  LicenseOverlay.h
   ------------------------------------------------------------------------------
   与「OH-Tai Ji 通道条」同款的激活机制（原生 C++ 面板版）。

   太极原版用 WebView 加载内嵌网页做激活窗；Fusion EQ 的 CMake 关掉了
   JUCE_WEB_BROWSER，无法复用 WebView，因此这里用原生控件还原同一套表单：
     · 未激活：整块画布被覆盖层挡住，不可关闭（唯一退出路径是激活成功），
       显示机器码 + 激活码输入框 + 激活按钮 + 前往官网购买/认领链接 + 状态提示。
     · 已激活：覆盖层不再拦截鼠标（插件照常可用），仅在角落显示
       「已激活」+ 机器码 + 退出登录。

   授权门控在处理器里（processBlock 未授权整条链静音），这里只负责
   收集激活码 / 触发联网 / 刷新门控与界面。后端逻辑全部走 taiji::LicenseManager，
   与产品 ID 绑定的 ohaudio.cn 账户系统对接（产品 ID = "FusionEQ"）。
============================================================================= */
#pragma once
#include <JuceHeader.h>
#include "LicenseManager.h"

class FusionEQAudioProcessor;   // 前向声明，避免把整个处理器头拖进来

class LicenseOverlay : public juce::Component
{
public:
    explicit LicenseOverlay (FusionEQAudioProcessor& p);
    ~LicenseOverlay() override;

    /** 读本地凭证刷新界面与处理器门控（不联网）。构造 / 激活成功 / 回验结果后调用。 */
    void refreshState();

    /** 启动时的联网回验（如 shouldVerifyOnLaunch 认为需要）：在后台线程跑 verify，
       结果回消息线程刷新。未激活时不会发起网络请求。 */
    void startLaunchVerify();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void doActivate (const juce::String& code);
    void onResult (const juce::var& res);   // 消息线程：刷新状态 + 提示
    static juce::var makeNetError();        // 线程内异常时构造一个 NETWORK 错误对象

    FusionEQAudioProcessor& processor;

    bool active = false;   // 是否已激活（决定是否拦截鼠标）
    bool busy   = false;   // 激活/回验进行中，防重入
    bool opIsActivate = true;   // 当前后台操作是「激活」(true) 还是「启动回验」(false)

    /* —— 锁定态控件（未激活时可见、拦截鼠标）—— */
    juce::Label      titleLabel;        // 「Fusion EQ」
    juce::Label      subtitleLabel;     // 「需要激活以继续使用」
    juce::Label      machineLabel;      // 机器码
    juce::TextEditor codeEditor;        // 激活码输入
    juce::TextButton activateButton { "激活" };
    juce::HyperlinkButton buyButton;    // 前往官网购买 / 认领
    juce::Label      statusLabel;       // 状态 / 错误提示

    /* —— 已激活态控件（角落徽标）—— */
    juce::Label      activatedLabel;    // 「已激活」
    juce::Label      badgeMachineLabel; // 机器码
    juce::TextButton logoutButton  { "退出登录" };

    /* 已激活徽标卡片矩形（resized 计算，paint 使用） */
    juce::Rectangle<float> badgeRect;

    /* 配色 */
    static constexpr juce::uint32 kBackdrop = 0xe01d242b;  // 半透明深色背景
    static constexpr juce::uint32 kCard     = 0xff232a31;  // 卡片底色
    static constexpr juce::uint32 kAccent   = 0xff2f9e6a;  // 激活/通过绿
    static constexpr juce::uint32 kWarn     = 0xffd98324;  // 错误橙
    static constexpr juce::uint32 kText     = 0xffe8ecf0;  // 卡片浅字
    static constexpr juce::uint32 kText2    = 0xff9aa3ac;  // 卡片次字
};
