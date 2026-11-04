#pragma once

#include <juce_core/juce_core.h>

// ============================================================================
// 产品身份 —— 由 CMakeLists.txt 注入，复制本工程做新插件时只改 CMake 两行。
//
//   OHAUDIO_PRODUCT_ID      必须与服务端 bootstrap.php 里 PRODUCTS 的 key 完全一致
//   OHAUDIO_PRODUCT_VERSION 与 CMake project(VERSION ...) 一致
//
// ★ 产品 ID 一旦有激活码发出就永远不许改、不许复用。要改显示名改服务端 name，
//   不要动 ID —— 已发出的码、license_key 签名里写的都是这个字符串。
// ============================================================================

#ifndef OHAUDIO_PRODUCT_ID
 #define OHAUDIO_PRODUCT_ID "FusionEQ"
#endif

// 兜底值，正常情况下由 CMake 注入 ${PROJECT_VERSION}。
// 只有脱离 CMake 单独编译某个源文件时才会用到，务必跟 CMakeLists 同步。
#ifndef OHAUDIO_PRODUCT_VERSION
 #define OHAUDIO_PRODUCT_VERSION "1.0.15"
#endif

namespace taiji
{
namespace product
{
inline juce::String id()
{
    static const juce::String s (OHAUDIO_PRODUCT_ID);
    return s;
}

inline juce::String version()
{
    static const juce::String s (OHAUDIO_PRODUCT_VERSION);
    return s;
}

/** 授权凭证文件按产品隔离：taiji.lic / fusioneq.lic / ...
 *
 *  多个插件若共用一份 .lic，A 插件激活会把 B 插件一起解锁（或互相覆盖）。
 *  文件名只保留字母数字，避免产品 ID 里的空格/符号落成一个奇怪的路径。
 */
inline juce::String licenseFileName()
{
    return id().toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789")
           + ".lic";
}

/** 机器指纹必须全产品共用，文件名不带产品名。
 *
 *  同一台物理机器应当是同一个 machine_id：服务器按它记设备名额，
 *  若每个插件各生成一个 ID，一张 2 设备上限的码装 3 个插件就爆了。
 */
inline juce::String machineIdFileName()
{
    return "machine.id";
}

/** 1.1.1 之前用的是 taiji.mid；读到就迁到统一文件名，保证 machine_id 不变。 */
inline juce::String legacyMachineIdFileName()
{
    return "taiji.mid";
}

} // namespace product
} // namespace taiji
