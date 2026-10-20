/* =============================================================================
   Fusion EQ  ·  Shaper.h
   一阶高通（用于 18 dB/oct 低切）+ 过采样饱和染色级

   ⚠ 增益一致性约定：
   所有饱和曲线都满足 y(0)=0 且 y'(0)=1（小信号斜率为 1）。
   这样"切到某个模式"不会偷偷改变音量 —— 之前用 tanh(k·x)·(1/tanh k)
   做满量程归一化，会把小信号放大 2.5 倍，正是 EQP/NEVE/API 音量暴涨失真的根因。
   ========================================================================== */
#pragma once
#include <JuceHeader.h>
#include "Defs.h"

namespace Fusion
{

/* ---------------------------------------------------------------------------
   一阶 6 dB/oct 高通
   H(z) = g (1 - z^-1) / (1 - r z^-1),  g = (1+r)/2,  r = exp(-2*pi*fc/fs)
   用于与二阶 biquad 级联，凑出 SSL / Neve 的 18 dB/oct。
--------------------------------------------------------------------------- */
class OnePoleHP
{
public:
    void reset() noexcept { x1 = 0.0f; y1 = 0.0f; }

    void setCutoff (double freq, double sampleRate) noexcept
    {
        const auto r = std::exp (-2.0 * juce::MathConstants<double>::pi * freq / sampleRate);
        b0 =  0.5f * float (1.0 + r);
        b1 = -0.5f * float (1.0 + r);
        a1 = float (r);
    }

    inline float processSample (float x) noexcept
    {
        const auto y = b0 * x + b1 * x1 + a1 * y1;
        x1 = x;
        y1 = y;
        return y;
    }

    /* 幅频响应 |H(e^jw)| */
    float getMagnitudeForFrequency (double f, double sampleRate) const noexcept
    {
        const auto w  = 2.0 * juce::MathConstants<double>::pi * f / sampleRate;
        const auto cw = std::cos (w), sw = std::sin (w);

        const auto nr = b0 + b1 * float (cw);
        const auto ni =      b1 * float (sw);
        const auto dr = 1.0f - a1 * float (cw);
        const auto di =     - a1 * float (sw);

        const auto nm = std::sqrt (double (nr * nr + ni * ni));
        const auto dm = std::sqrt (double (dr * dr + di * di));
        return float (dm > 1.0e-9 ? nm / dm : 0.0);
    }

private:
    float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f;
    float x1 = 0.0f, y1 = 0.0f;
};

/* ---------------------------------------------------------------------------
   归一化软饱和：y = x / (1 + |x|^n)^(1/n)
     · y'(0) = 1        → 小信号零增益变化
     · |y| < 1          → 峰值渐进压到 ±1，永不爆表
     · n 越小拐点越软（压缩越多），n 越大越接近线性
     · nPos ≠ nNeg      → 正负半周不对称，产生偶次谐波（电子管味）
--------------------------------------------------------------------------- */
inline float softSat (float x, float nPos, float nNeg) noexcept
{
    const auto a = std::abs (x);
    const auto n = x >= 0.0f ? nPos : nNeg;
    return x / std::pow (1.0f + std::pow (a, n), 1.0f / n);
}

inline std::function<float (float)> makeShaperFunction (Shaper type, float amount) noexcept
{
    switch (type)
    {
        case Shaper::Linear:
            return [] (float x) { return x; };

        case Shaper::Tube:                       // Manley：最软的拐点 + 明显不对称
        {
            const auto n = juce::jlimit (1.2f, 6.0f, 3.40f - amount * 1.90f);
            const auto nn = n * 1.18f;
            return [n, nn] (float x) { return softSat (x, n, nn); };
        }

        case Shaper::TubeLite:                   // Pultec 末级管：轻微圆润
        {
            const auto n = juce::jlimit (2.0f, 8.0f, 4.20f - amount * 2.00f);
            const auto nn = n * 1.08f;
            return [n, nn] (float x) { return softSat (x, n, nn); };
        }

        case Shaper::Vca:                        // SSL：几乎线性，只在末端快速收住
        {
            const auto t = juce::jlimit (0.55f, 0.95f, 0.92f - amount * 0.28f);
            return [t] (float x)
            {
                const auto a = std::abs (x);
                if (a <= t) return x;
                const auto v = t + (1.0f - t) * std::tanh ((a - t) / (1.0f - t));
                return std::copysign (v, x);
            };
        }

        case Shaper::Transformer:                // Neve：拐点略硬 + 不对称更重
        {
            const auto n = juce::jlimit (1.4f, 6.0f, 3.40f - amount * 1.80f);
            const auto nn = n * 1.15f;
            return [n, nn, amount] (float x)
            {
                const auto y = softSat (x, n, nn);
                /* 极轻的三次项，补一点变压器"密度"，不影响小信号电平 */
                return y + 0.030f * amount * (x * x * x - 0.15f * x);
            };
        }

        case Shaper::OpAmp:                      // API：对称、硬朗
        {
            const auto n = juce::jlimit (1.2f, 5.0f, 2.60f - amount * 1.40f);
            return [n] (float x) { return softSat (x, n, n); };
        }

        default:
            return [] (float x) { return x; };
    }
}

/* ---------------------------------------------------------------------------
   单声道过采样染色级
   factor = 2 表示 2^2 = 4× 过采样，把饱和产生的混叠推到可闻频段之外。
--------------------------------------------------------------------------- */
class ColorStage
{
public:
    void prepare (double sampleRate, int maxBlockSize)
    {
        osSampleRate = sampleRate;
        osMaxBlock   = maxBlockSize;
        rebuildOS();
        juce::ignoreUnused (sampleRate);
    }

    void reset() noexcept { if (os != nullptr) os->reset(); }

    int getLatencySamples() const noexcept
    {
        return os != nullptr ? (int) std::round (os->getLatencyInSamples()) : 0;
    }

    void setMode (Shaper type, float amount, float drive, float makeup)
    {
        shaper = makeShaperFunction (type, amount);
        preGain  = juce::jmax (0.1f, drive);
        /* 除以 preGain 抵消推入量 → 小信号整体增益恒等于 makeup */
        postGain = makeup / preGain;
    }

    /* 过采样倍率：0 = X1（基率直通，零延迟）；1 = X2；2 = X4；3 = X8；4 = X16。
       倍率越高音质越好（混叠越远），延迟也越大。切换时重建并清空滤波器状态，
       避免半带滤波器里的旧样本喷出来。 */
    void setOversamplePower (int p)
    {
        p = juce::jlimit (0, 4, p);

        if (p != osPow)
        {
            osPow = p;
            rebuildOS();
        }
    }

    int getOversamplePower() const noexcept { return osPow; }

    /* 原地处理一个声道的整块样本 */
    void processBlock (float* data, int numSamples) noexcept
    {
        /* X1：直接在基率上饱和（零延迟，高频可能有轻微混叠） */
        if (osPow == 0 || os == nullptr)
        {
            for (int i = 0; i < numSamples; ++i)
                data[i] = postGain * shaper (preGain * data[i]);
            return;
        }

        float* ch[1] { data };
        juce::dsp::AudioBlock<float> block (ch, 1, (size_t) numSamples);

        auto up = os->processSamplesUp (block);
        auto* d = up.getChannelPointer (0);
        const auto n = up.getNumSamples();

        for (size_t i = 0; i < n; ++i)
            d[i] = postGain * shaper (preGain * d[i]);

        os->processSamplesDown (block);
    }

private:
    void rebuildOS()
    {
        os.reset();

        if (osPow > 0)
        {
            os = std::make_unique<juce::dsp::Oversampling<float>> (
                1, osPow,
                juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                true);                               // 整数延迟，便于宿主补偿
            os->initProcessing ((size_t) juce::jmax (1, osMaxBlock));
            os->reset();
        }
    }

    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    std::function<float (float)> shaper = [] (float x) { return x; };
    float preGain = 1.0f, postGain = 1.0f;
    int   osPow      = 4;      // 0=X1 1=X2 2=X4 3=X8 4=X16(默认)
    int   osMaxBlock = 0;
    double osSampleRate = 48000.0;
};

} // namespace Fusion
