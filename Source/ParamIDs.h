/* =============================================================================
   Fusion EQ  ·  ParamIDs.h
   参数 ID —— Processor 与 Editor 共用
   ========================================================================== */
#pragma once
#include <JuceHeader.h>

namespace IDs
{
    static constexpr const char* mode     = "mode";
    static constexpr const char* hpfOn    = "hpfOn";
    static constexpr const char* lpOn     = "lpOn";       // 硬朗 215L：LO-PASS（低切）独立开关
    static constexpr const char* hpfFreq  = "hpfFreq";   // 低切频点步进选择 50/80/160/300 Hz
    static constexpr const char* airOn    = "airOn";
    static constexpr const char* airAmt   = "airAmt";
    static constexpr const char* hpfSlope = "hpfSlope";   // 低切斜率 12/24/36 dB/oct
    static constexpr const char* filtHp   = "filtHp";     // SSL 滤波 HP 频点（低切）
    static constexpr const char* filtLp   = "filtLp";     // SSL 滤波 LP 频点（高切）
    static constexpr const char* autoGain = "autoGain";   // 自动增益补偿（取代原 INPUT 旋钮）
    static constexpr const char* osMode   = "osMode";     // 过采样倍率选择 X1/X2/X4
    static constexpr const char* output   = "output";

    /* Pultec EQP-1A 三段式（温润模式专属） */
    static constexpr const char* plLowFreq   = "plLowFreq";    // 20/30/60/100 Hz
    static constexpr const char* plLowBoost  = "plLowBoost";   // 0-10
    static constexpr const char* plLowAtten  = "plLowAtten";   // 0-10
    static constexpr const char* plBand      = "plBand";       // 线性带宽 0=BROAD 1=SHARP
    static constexpr const char* plHighFreq  = "plHighFreq";   // 5/8/10/12/16 kHz
    static constexpr const char* plHighBoost = "plHighBoost";  // 0-10
    static constexpr const char* plHighAtten    = "plHighAtten";     // 5/10/20 KC 频点选择
    static constexpr const char* plHighAttenAmt = "plHighAttenAmt";  // 0-10 衰减量

    inline juce::String bType (int i) { return "band" + juce::String (i) + "Type"; }
    inline juce::String bFreq (int i) { return "band" + juce::String (i) + "Freq"; }
    inline juce::String bGain (int i) { return "band" + juce::String (i) + "Gain"; }
    inline juce::String bQ    (int i) { return "band" + juce::String (i) + "Q"; }
    inline juce::String bOn   (int i) { return "band" + juce::String (i) + "On"; }
    /* 低切 / 高切段专属：斜率级数 0=12 1=24 2=36 dB/oct（其余类型忽略） */
    inline juce::String bSlope(int i) { return "band" + juce::String (i) + "Slope"; }

    /* 动态压缩（干净模式专属，F6 式每段动态 EQ；其它模式忽略这些参数） */
    inline juce::String bDynOn  (int i) { return "band" + juce::String (i) + "DynOn";  }
    inline juce::String bDynThr (int i) { return "band" + juce::String (i) + "DynThr"; }
    inline juce::String bDynRng (int i) { return "band" + juce::String (i) + "DynRng"; }
    inline juce::String bDynAtt (int i) { return "band" + juce::String (i) + "DynAtt"; }
    inline juce::String bDynRel (int i) { return "band" + juce::String (i) + "DynRel"; }

    /* 每段声道模式（Pro-Q 式）：0=Stereo 1=Left 2=Right 3=Mid 4=Side
       （干净模式接入信号链，其它模式强制 Stereo） */
    inline juce::String bChan (int i) { return "band" + juce::String (i) + "Chan"; }
}
