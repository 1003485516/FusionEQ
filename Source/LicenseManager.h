#pragma once

#include "ProductConfig.h"

#include <juce_core/juce_core.h>

namespace taiji
{
/** 授权管理：机器指纹 + 本地授权文件 + 与 ohaudio.cn 后端对接。
 *
 *  只对接 API.md（oh-account）里的 activate / verify 两个接口；license_key 原样存本地，
 *  插件不解析也不验签（密钥只在服务器）。离线优先：启动读本地即可放行，
 *  联网校验交给 verify 判吊销 / 过期。
 *
 *  线程：activate / verify 会做阻塞的网络 I/O，调用方必须在非消息线程里跑
 *  （见 LicenseOverlay 用 std::thread 包一层），拿到结果再回消息线程刷新 UI。 */
class LicenseManager
{
public:
    /** 后端基地址（见 oh-account/API.md）。 */
    static const juce::String& apiBase();

    /** 稳定的机器指纹：首次生成后写进用户数据目录，之后每次读同一份。
     *  用持久化 UUID 而非每次随机 —— 避免重装 / 重启被判成新设备（API.md 第 5 节）。 */
    static juce::String getMachineId();

    /** 本地是否已有授权文件。 */
    static bool hasLicense();

    /** 当前授权状态（不联网）。返回 var：
     *  { activated:bool, product, plan, expiresAt, features:[], maxDevices,
     *    code, machineId, savedAt }
     *  注意：license_key 绝不从这里出去，UI 侧拿不到。 */
    static juce::var getState();

    /** 调 activate 拿 license_key 存本地。返回服务器响应的 var 原样
     *  （含 ok / license / error / code），便于 UI 直接映射文案。
     *  只凭激活码 + 机器指纹，不传账号：用户归属由官网 claim 在服务器端绑定。 */
    static juce::var activate (const juce::String& code,
                               const juce::String& deviceName,
                               const juce::String& os,
                               const juce::String& plugin,
                               const juce::String& version);

    /** 调 verify 检测吊销 / 过期。读取本地 license_key + machine_id；
     *  成功返回 {ok:true,status,...}；UNBOUND / LICENSE_EXPIRED / LICENSE_INVALID
     *  会清掉本地授权并返回服务器错误；网络 / 解析失败返回 {ok:false,code:"NETWORK"/"PARSE"}。 */
    static juce::var verify();

    /** 清掉本地授权（退出登录 / 解绑本机）。 */
    static void logout();

    /** 启动时是否需要联网校验：本地无授权 / 已过期 / 距上次校验超 7 天 → true。
     *  离线优先：未过期且近期已校验过就不打扰网络。 */
    static bool shouldVerifyOnLaunch();

    /** 插件版本号（随 CMake project 版本走，手动同步）。 */
    static juce::String version();

private:
    static juce::File licenseDir();
    static juce::File licenseFile();
    static juce::File machineIdFile();

    // 本地授权文件读写（XOR + base64 混淆，尽量不裸存 license_key 明文）
    static juce::var readLicense();
    static void saveLicense (const juce::var& license);
    static void touchLastVerify();

    static juce::var postJson (const juce::String& action, const juce::var& body);
    static juce::var errObj (const juce::String& code, const juce::String& msg);

    static juce::String scramble (const juce::String&);
    static juce::String unscramble (const juce::String&);
};
} // namespace taiji
