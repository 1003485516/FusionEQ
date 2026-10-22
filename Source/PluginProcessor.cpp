/* =============================================================================
   Fusion EQ  ·  PluginProcessor.cpp
   ========================================================================== */
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamIDs.h"

using Coeffs = juce::dsp::IIR::Coefficients<float>;

/* 让一个单声道滤波器处理整块的辅助函数 */
static void runFilter (juce::dsp::IIR::Filter<float>& f, float* data, int numSamples) noexcept
{
    float* ch[1] { data };
    juce::dsp::AudioBlock<float> block (ch, 1, (size_t) numSamples);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    f.process (ctx);
}

/* ============================== 构造 ============================== */
FusionEQAudioProcessor::FusionEQAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
#else
    : AudioProcessor ()
#endif
    , apvts (*this, nullptr, juce::Identifier ("FusionEQ"), createParameterLayout())
{
    using namespace Fusion;
    const auto& spec = getSpec (Mode::Clean);

    for (int i = 0; i < numBands; ++i)
    {
        auto& b = bands[(size_t) i];
        b.type  = (int) spec.bands[(size_t) i].type;
        b.freq.setCurrentAndTargetValue (spec.bands[(size_t) i].freq);
        b.q.setCurrentAndTargetValue    (spec.bands[(size_t) i].q);
        b.gain.setCurrentAndTargetValue (spec.bands[(size_t) i].gainDb);
    }

    /* 预计算频响曲线的对数频点表 */
    for (int i = 0; i < curvePoints; ++i)
        curveFreqs[(size_t) i] = fMin * std::pow (fMax / fMin,
                                                  (float) i / (float) (curvePoints - 1));

    /* 每模式参数快照：以各模式 spec 默认值起步 */
    initModeStates();
}

/* ============================ 每模式参数快照 ============================ */
void FusionEQAudioProcessor::initModeStates()
{
    for (int m = 0; m < Fusion::numModes; ++m)
    {
        auto& st = modeStates[(size_t) m];
        const auto& sp = getSpec ((Fusion::Mode) m);

        for (int b = 0; b < Fusion::numBands; ++b)
        {
            const auto& bd = sp.bands[(size_t) b];
            st.type[b] = (int) bd.type;
            st.freq[b] = bd.freq;
            st.gain[b] = bd.gainDb;
            st.q[b]    = bd.q;
            /* SSL 原版无段开关（常开）；其它模式与 bOn 参数默认一致 = 关 */
            st.on[b]   = ((Fusion::Mode) m) == Fusion::Mode::SSL;
        }

        st.hpfOn    = false;   // 硬朗 HP/LP 默认关闭：不处理，拧到档位再手动打开
        st.lpOn     = false;
        st.hpfSlope = 0;
        st.filtHp   = 20.0f;      // 端点 = OUT（旁通）
        st.filtLp   = 20000.0f;
        st.airOn    = false;
        st.airAmt   = 0.0f;
    }
}

void FusionEQAudioProcessor::captureModeState (int m)
{
    if (m < 0 || m >= Fusion::numModes)
        return;

    auto& st = modeStates[(size_t) m];

    for (int b = 0; b < Fusion::numBands; ++b)
    {
        st.type[b] = (int) std::round (apvts.getRawParameterValue (IDs::bType (b))->load());
        st.freq[b] = apvts.getRawParameterValue (IDs::bFreq (b))->load();
        st.gain[b] = apvts.getRawParameterValue (IDs::bGain (b))->load();
        st.q[b]    = apvts.getRawParameterValue (IDs::bQ    (b))->load();
        st.slope[b] = apvts.getRawParameterValue (IDs::bSlope (b))->load();
        st.on[b]   = apvts.getRawParameterValue (IDs::bOn   (b))->load() > 0.5f;
    }

    st.hpfOn    = apvts.getRawParameterValue (IDs::hpfOn)->load() > 0.5f;
    st.lpOn     = apvts.getRawParameterValue (IDs::lpOn)->load() > 0.5f;
    st.hpfSlope = (int) std::round (apvts.getRawParameterValue (IDs::hpfSlope)->load());
    st.hpfFreq  = (int) std::round (apvts.getRawParameterValue (IDs::hpfFreq)->load());
    st.filtHp   = apvts.getRawParameterValue (IDs::filtHp)->load();
    st.filtLp   = apvts.getRawParameterValue (IDs::filtLp)->load();
    st.airOn    = apvts.getRawParameterValue (IDs::airOn)->load() > 0.5f;
    st.airAmt   = apvts.getRawParameterValue (IDs::airAmt)->load();
}

void FusionEQAudioProcessor::restoreModeState (int m)
{
    if (m < 0 || m >= Fusion::numModes)
        return;

    const auto& st = modeStates[(size_t) m];

    auto setF = [this] (const juce::String& id, float v)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 (v));
    };

    for (int b = 0; b < Fusion::numBands; ++b)
    {
        setF (IDs::bType (b), (float) st.type[b]);
        setF (IDs::bFreq (b), st.freq[b]);
        setF (IDs::bGain (b), st.gain[b]);
        setF (IDs::bQ    (b), st.q[b]);
        setF (IDs::bSlope (b), st.slope[b]);
        setF (IDs::bOn   (b), st.on[b] ? 1.0f : 0.0f);
    }

    setF (IDs::hpfOn,    st.hpfOn ? 1.0f : 0.0f);
    setF (IDs::lpOn,     st.lpOn ? 1.0f : 0.0f);
    setF (IDs::hpfSlope, (float) st.hpfSlope);
    setF (IDs::hpfFreq,  (float) st.hpfFreq);
    setF (IDs::filtHp,   st.filtHp);
    setF (IDs::filtLp,   st.filtLp);
    setF (IDs::airOn,    st.airOn ? 1.0f : 0.0f);
    setF (IDs::airAmt,   st.airAmt);
}

FusionEQAudioProcessor::~FusionEQAudioProcessor() = default;

/* ============================ 参数布局 ============================ */
juce::AudioProcessorValueTreeState::ParameterLayout
FusionEQAudioProcessor::createParameterLayout()
{
    using namespace Fusion;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    juce::StringArray modeNames;
    for (auto& s : getModeSpecs())
        modeNames.add (s.en + U8 (" \u00b7 ") + s.cn);
    layout.add (std::make_unique<juce::AudioParameterChoice> (IDs::mode, "Mode", modeNames, 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (IDs::hpfOn, "Low Cut", false));  // 默认关闭
    layout.add (std::make_unique<juce::AudioParameterBool> (IDs::lpOn, "Low Cut LP", false)); // 硬朗 215L LO-PASS 独立开关（默认关闭）
    layout.add (std::make_unique<juce::AudioParameterChoice> (IDs::hpfFreq, "Low Cut Freq",
                juce::StringArray { "50 Hz", "80 Hz", "160 Hz", "300 Hz" }, 1));
    layout.add (std::make_unique<juce::AudioParameterChoice> (IDs::hpfSlope, "Low Cut Slope",
                juce::StringArray { "12 dB/oct", "24 dB/oct", "36 dB/oct" }, 0));

    /* SSL 滤波（原版 4000 通道 HP/LP：固定 12 dB/oct，端点 = OUT 旁通） */
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::filtHp, "SSL HP Freq",
                juce::NormalisableRange<float> (20.0f, 2000.0f, 0.0f, 0.25f), 20.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::filtLp, "SSL LP Freq",
                juce::NormalisableRange<float> (200.0f, 20000.0f, 0.0f, 0.25f), 20000.0f));

    const auto& def0 = getSpec (Mode::Clean);

    for (int i = 0; i < Fusion::numBands; ++i)
    {
        const juce::String n = "Band " + juce::String (i + 1) + " ";
        const auto& d = def0.bands[(size_t) i];

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            IDs::bType (i), n + "Type",
            juce::StringArray { "Bell", "Low Shelf", "Low Cut", "High Shelf", "High Cut",
                                "Notch", "Band Pass", "Tilt Shelf", "All Pass" },
            (int) d.type));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            IDs::bFreq (i), n + "Freq",
            juce::NormalisableRange<float> (fMin, fMax, 0.0f, 0.25f), d.freq));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            IDs::bGain (i), n + "Gain",
            juce::NormalisableRange<float> (-gainRange, gainRange, 0.01f), d.gainDb));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            IDs::bQ (i), n + "Q",
            juce::NormalisableRange<float> (qMin, qMax, 0.0f, 0.35f), d.q));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            IDs::bOn (i), n + "On", false));

        /* 低切 / 高切段斜率：离散档位 6~192 dB/oct（滚轮逐档，末档 BRICKWALL） */
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            IDs::bSlope (i), n + "Slope",
            juce::NormalisableRange<float> (6.0f, 192.0f, 0.0f, 0.05f), 6.0f));

        /* 动态压缩（F6 式每段动态 EQ，仅干净模式接入信号链）：
           阈值 / 作用范围 / 启动 / 释放；默认关闭 */
        layout.add (std::make_unique<juce::AudioParameterBool> (
            IDs::bDynOn (i), n + "Dyn On", false));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            IDs::bDynThr (i), n + "Dyn Thr",
            juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f, 1.5f), -40.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            IDs::bDynRng (i), n + "Dyn Range",
            juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f), 6.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            IDs::bDynAtt (i), n + "Dyn Attack",
            juce::NormalisableRange<float> (0.5f, 150.0f, 0.0f, 0.35f), 8.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            IDs::bDynRel (i), n + "Dyn Release",
            juce::NormalisableRange<float> (10.0f, 1500.0f, 0.0f, 0.35f), 150.0f));

        /* 每段声道模式（Pro-Q 式）：Stereo / Left / Right / Mid / Side */
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            IDs::bChan (i), n + "Channel",
            juce::StringArray { "Stereo", "Left", "Right", "Mid", "Side" }, 0));
    }

    layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::airOn, "Air On", false));  // 默认关闭
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::airAmt, "Air Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

    /* Pultec EQP-1A 三段式（温润模式专属）：boost 与 atten 独立，可同时作用 */
    layout.add (std::make_unique<juce::AudioParameterChoice> (IDs::plLowFreq, "Pultec Low Freq",
                juce::StringArray { "20 CPS", "30 CPS", "60 CPS", "100 CPS" }, 1));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::plLowBoost, "Pultec Low Boost",
                juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::plLowAtten, "Pultec Low Atten",
                juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::plBand, "Pultec Bandwidth",
                juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (IDs::plHighFreq, "Pultec High Freq",
                juce::StringArray { "5 KCS", "8 KCS", "10 KCS", "12 KCS", "16 KCS" }, 2));
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::plHighBoost, "Pultec High Boost",
                juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 0.0f));
    /* ATTEN SEL：三档步进（Pultec 原机风格），选择高切 shelf 转折频率 5/10/20 KC */
    layout.add (std::make_unique<juce::AudioParameterChoice> (IDs::plHighAtten, "Pultec Atten Sel",
                juce::StringArray { "5", "10", "20" }, 0));
    /* ATTEN：0-10 连续衰减量旋钮 */
    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::plHighAttenAmt, "Pultec Atten",
                juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 0.0f));

    /* 自动增益补偿（取代原 INPUT 旋钮） */
    layout.add (std::make_unique<juce::AudioParameterBool> (IDs::autoGain, "Auto Gain", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (IDs::osMode, "Oversample",
                juce::StringArray { "X1", "X2", "X4", "X8", "X16" }, 4));   // 默认 X16

    layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::output, "Output",
        juce::NormalisableRange<float> (-gainRange, gainRange, 0.1f), 0.0f));

    return layout;
}

/* ============================ 准备播放 ============================ */
void FusionEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    const juce::dsp::ProcessSpec pspec { sampleRate, (juce::uint32) samplesPerBlock, 2 };
    const double ramp = 0.025;   // 25 ms 参数平滑，避免拉链噪声
    const auto& modeSpec = Fusion::getSpec (getMode());

    for (auto& b : bands)
    {
        b.freq.reset (sampleRate, ramp);
        b.q.reset    (sampleRate, ramp);
        b.gain.reset (sampleRate, ramp);

        for (auto& f : b.filters)
        {
            f.prepare (pspec);
            f.reset();
        }

        for (auto& row : b.slopeFilters)
            for (auto& f : row)
            {
                f.prepare (pspec);
                f.reset();
            }

        for (auto& op : b.slopeFirst)
            op.reset();

        for (auto& f : b.detFilters)
        {
            f.prepare (pspec);
            f.reset();
        }

        b.env = 0.0f;
        b.grDb = 0.0f;
        b.dirty = true;
    }

    for (auto& t : tilt)
        for (auto& f : t.f) { f.prepare (pspec); f.reset(); }

    for (auto& stage : hpf.biquad)
        for (auto& h : stage)
        { h.prepare (pspec); h.reset(); }
    for (auto& c : hpf.character) { c.prepare (pspec); c.reset(); }
    for (auto& o : hpf.onePole) o.reset();

    for (auto& f : filt.hp) { f.prepare (pspec); f.reset(); }
    for (auto& f : filt.lp) { f.prepare (pspec); f.reset(); }
    for (auto& f : filtLp1) { f.prepare (pspec); f.reset(); }
    rebuildFilt();

    for (auto& stage : airHP)
        for (auto& f : stage) { f.prepare (pspec); f.reset(); }

    for (auto* shelf : { &plLowBoostF, &plLowAttenF, &plHighBoostF, &plHighAttenF })
        for (auto& f : shelf->ch) { f.prepare (pspec); f.reset(); }

    for (auto& lf : listenFilters) { lf.prepare (pspec); lf.reset(); }
    lastListenBand = -1; lastListenF = -1.0f; lastListenQ = -1.0f;

    osPow = juce::jlimit (0, 4, juce::roundToInt (
        apvts.getRawParameterValue (IDs::osMode)->load()));

    for (auto& c : color)
    {
        c.prepare (sampleRate, samplesPerBlock);
        c.setMode (modeSpec.shaper, modeSpec.amount, modeSpec.drive, modeSpec.makeup);
        c.setOversamplePower (osPow);
    }

    /* 向宿主声明过采样引入的延迟（随倍率变化，切倍率时会在处理里更新） */
    setLatencySamples (color[0].getLatencySamples());

    outGain.reset (sampleRate, ramp);
    airGain.reset (sampleRate, 0.01);

    /* 自动增益补偿：40 ms 平滑（只消除拧旋钮的台阶，不跟随音乐起伏） */
    autoGain.reset (sampleRate, 0.04);
    autoGain.setCurrentAndTargetValue (1.0f);
    autoGainDb.store (0.0f);

    lastMode  = getMode();
    hpfOn     = apvts.getRawParameterValue (IDs::hpfOn)->load() > 0.5f;
    lpOn      = apvts.getRawParameterValue (IDs::lpOn)->load() > 0.5f;
    lastHpfSlope = juce::jlimit (0, 2, juce::roundToInt (
        apvts.getRawParameterValue (IDs::hpfSlope)->load()));
    lastHpfFreq = juce::jlimit (0, 3, juce::roundToInt (
        apvts.getRawParameterValue (IDs::hpfFreq)->load()));
    hpfFreqHz = (lastHpfFreq == 0 ? 50.0f : lastHpfFreq == 2 ? 160.0f
                 : lastHpfFreq == 3 ? 300.0f : 80.0f);
    airOn     = apvts.getRawParameterValue (IDs::airOn)->load() > 0.5f;
    airAmt    = apvts.getRawParameterValue (IDs::airAmt)->load();
    autoGainOn = apvts.getRawParameterValue (IDs::autoGain)->load() > 0.5f;

    rebuildHpf();
    rebuildAir();
    plLowBoostV = apvts.getRawParameterValue (IDs::plLowBoost)->load();
    plLowAttenV = apvts.getRawParameterValue (IDs::plLowAtten)->load();
    plHighBoostV = apvts.getRawParameterValue (IDs::plHighBoost)->load();
    plHighAttenI = juce::roundToInt (apvts.getRawParameterValue (IDs::plHighAtten)->load());
    plHighAttenAmtV = apvts.getRawParameterValue (IDs::plHighAttenAmt)->load();
    plLowFreqI  = juce::roundToInt (apvts.getRawParameterValue (IDs::plLowFreq)->load());
    plHighFreqI = juce::roundToInt (apvts.getRawParameterValue (IDs::plHighFreq)->load());
    plBandV     = apvts.getRawParameterValue (IDs::plBand)->load();
    rebuildPultec();
    updateTiltCoefficients();

    for (int i = 0; i < Fusion::numBands; ++i)
    {
        /* 斜率先从参数取，再建系数（updateBandCoefficients 会按它算节数） */
        bands[(size_t) i].slopeDb = juce::jlimit (
            6.0f, 192.0f, apvts.getRawParameterValue (IDs::bSlope (i))->load());
        updateBandCoefficients (i);
    }

    updateResponseSnapshot();

    airScratch.setSize (2, samplesPerBlock, false, false, true);
    mpSum .setSize (2, samplesPerBlock, false, false, true);
    mpBand.setSize (2, samplesPerBlock, false, false, true);
    dynDry .setSize (2, samplesPerBlock, false, false, true);
    dynDet .setSize (2, samplesPerBlock, false, false, true);
    dynBand.setSize (2, samplesPerBlock, false, false, true);

    for (auto& gc : dynGrCurve)
        gc.assign ((size_t) samplesPerBlock, 1.0f);

    /* Look-ahead：10 ms 前视（混音用途，一切以音质为主）。
       环形容量还必须能装下"整个处理块"：look-ahead 每块要推进 numSamples
       个位置，容量不足时块内读写会互相覆盖，读出的是本块刚写进去的值，
       延迟在块内从 laSize 一路塌缩到几十个采样 —— 表现为一开动态压缩
       就出现的金属/电容式杂音（宿主缓冲大于 10 ms 时必然触发）。 */
    laSize = juce::jmax (1, (int) std::round (currentSampleRate * 0.010), samplesPerBlock);
    laBuf.assign ((size_t) Fusion::numBands * 2u * (size_t) laSize, 0.0f);
    laPos = 0;
    spectrumBuf[0].fill (-100.0f);
    spectrumBuf[1].fill (-100.0f);
    specWork.fill (-100.0f);

    /* 频谱：环形历史缓冲 + 预计算频点映射 */
    fftHistory.setSize (1, fftSize, false, false, true);
    fftHistory.clear();
    histIndex = 0;
    hopCount  = 0;
    prepareSpectrumMap();
}

/* 把 768 个对数频点映射到 FFT bin 位置，只在采样率变化时算一次 */
void FusionEQAudioProcessor::prepareSpectrumMap()
{
    const auto nyq  = currentSampleRate * 0.5;
    const int  bins = fftSize / 2;
    const auto ratio = (double) Fusion::fMax / (double) Fusion::fMin;

    for (int i = 0; i < spectrumBins; ++i)
    {
        const double f0 = Fusion::fMin * std::pow (ratio, (double) i       / (double) spectrumBins);
        const double f1 = Fusion::fMin * std::pow (ratio, (double) (i + 1) / (double) spectrumBins);

        binPos0[(size_t) i] = f0 / nyq * (double) bins;
        binPos1[(size_t) i] = f1 / nyq * (double) bins;
    }

    spectrumMapReady = currentSampleRate > 0.0;
}

void FusionEQAudioProcessor::releaseResources() {}

bool FusionEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

/* ============================ 结构重建 ============================ */
/* 低切级联：纯 Butterworth 对齐 —— 各二阶节共用 60 Hz 转折点、
   Q 按阶数分布（2 阶: 0.707 / 4 阶: 0.541+1.307 / 6 阶: 0.518+0.707+1.932）。
   复合响应恒为 -3 dB @ fc，且阶数越高、100 Hz 以上归平越快。
   注意：模式性格绝不缩放这里的 Q —— 高 Q 级的谐振峰会随级数相乘
   （36 dB/oct 曾被放大到 +9 dB，几何平均也只能部分缓解）；
   性格改由独立的"性格节"实现，见 makeHpfCharacter。 */
static float hpfSectionQ (int stageIdx, int numStages) noexcept
{
    static constexpr float bw[3][3] = {
        { 0.7071f, 0.0f,    0.0f    },   // 2 阶（12 dB/oct）
        { 0.5412f, 1.3066f, 0.0f    },   // 4 阶（24 dB/oct）
        { 0.5176f, 0.7071f, 1.9319f }    // 6 阶（36 dB/oct）
    };

    return bw[(size_t) juce::jlimit (0, 2, numStages - 1)]
             [(size_t) juce::jlimit (0, 2, stageIdx)];
}

/* Butterworth 梯形分节 Q（截止段斜率档位用；units = 斜率/6，即总阶数 N）。
   偶数阶 2M（12/24/36…dB/oct）：
       θ_k = (2k+1)·π / (2N)，k = 0..M-1，Q_k = 1/(2·cos θ_k)
       —— 与原 16×16 硬编码表逐项一致（已数值核对）。
   奇数阶 2M+1（6/18/30…dB/oct）：
       M 个二阶级 + 1 个一阶节。二阶级取奇数阶的前 M 对共轭极点，
       θ_k = (k+1)·π / N（自负实轴起算）；最后一个极点是实极点，
       恰好落在截止频率上 → 由 OnePoleHP 一阶节承担。
       例：18 dB/oct → Q = 1.0；30 dB/oct → Q = 0.618 / 1.618。
   这样拼出的级联就是**精确的 N 阶 Butterworth**：
       · -3 dB 点严格落在截止频率上（不需要任何 fc 补偿）
       · 截止点上方严格归平（16 kHz 处误差 < 0.001 dB）
   旧实现奇数档错借了偶数阶的梯形 Q（18 档首节用了 0.5176 而非 1.0），
   只能靠把各级截止下压 0.82~0.86 倍去凑 -3 dB 点 —— 结果 18/30 档在
   100~500 Hz 留下 -0.3~-0.6 dB 的凹陷，就是用户看到的"频率往下掉"。
   BRICKWALL(192 dB/oct) = 32 阶：16 个二阶级，θ_0 = π/64 → Q=0.5006。 */
static float cutSlopeQ (int units, int sectionIdx) noexcept
{
    const int n = juce::jlimit (1, 32, units);
    const double theta = (n % 2 == 0)
        ? (2.0 * sectionIdx + 1.0) * juce::MathConstants<double>::pi / (2.0 * n)
        : (sectionIdx + 1.0) * juce::MathConstants<double>::pi / n;
    return (float) (1.0 / (2.0 * std::cos (theta)));
}

/* 模式肩部性格 → 独立二阶节（叠加在 Butterworth 级联之后）：
   - Q > 0.707（厚实 1.05 / 硬朗 1.15）：取该 Q 在 12 dB/oct 单节时的
     谐振峰值增益 2Q²/√(4Q²−1) 作为固定凸起（约 +1.5 / +2.1 dB）；
   - Q < 0.707（温暖 0.55 / 温润 0.50）：取转折点比 −3 dB 更深的量
     Q/0.707 作为固定凹陷（约 −2.2 / −3.0 dB），还原软肩部；
   - Q = 0.707（干净 / 通透）：全通。
   峰/谷中心 = 转折频率 60 Hz、Q 0.9 —— 无论选哪种斜率，性格幅度严格一致。 */
static Coeffs::Ptr makeHpfCharacter (double sr, float charQ, float cornerHz)
{
    constexpr float bw = 0.70710678f;
    auto gainLin = 1.0f;

    if (charQ > bw + 1.0e-3f)
        gainLin = (2.0f * charQ * charQ) / std::sqrt (4.0f * charQ * charQ - 1.0f);
    else if (charQ < bw - 1.0e-3f)
        gainLin = charQ / bw;

    return Coeffs::makePeakFilter (sr, cornerHz, 0.9f, gainLin);
}

void FusionEQAudioProcessor::rebuildHpf()
{
    const auto& spec = Fusion::getSpec (getMode());
    const int stages = 1 + juce::jlimit (0, 2, juce::roundToInt (
        apvts.getRawParameterValue (IDs::hpfSlope)->load()));

    for (int s = 0; s < 3; ++s)
    {
        hpfCoeffs[(size_t) s] = Coeffs::makeHighPass (currentSampleRate, hpfFreqHz,
                                                      hpfSectionQ (s, stages));
        for (auto& h : hpf.biquad[(size_t) s])
            h.coefficients = hpfCoeffs[(size_t) s];
    }

    if (auto c = makeHpfCharacter (currentSampleRate, spec.hpfQ, hpfFreqHz))
        for (auto& f : hpf.character)
            f.coefficients = c;

    for (auto& o : hpf.onePole)
        o.setCutoff ((double) hpfFreqHz, currentSampleRate);
}

/* SSL 滤波：HP/LP 各一节二阶（12 dB/oct），Butterworth Q = 1/√2；
   Manley：LOW PASS 原机 18 dB/oct → 二阶 + 一阶补一级（filtLp1） */
void FusionEQAudioProcessor::rebuildFilt()
{
    filtHpCoeffs = Coeffs::makeHighPass (currentSampleRate, filtHpHz, 0.7071f);
    filtLpCoeffs = Coeffs::makeLowPass  (currentSampleRate, filtLpHz, 0.7071f);
    filtLp1Coeffs = Coeffs::makeFirstOrderLowPass (currentSampleRate, filtLpHz);

    for (auto& f : filt.hp) f.coefficients = filtHpCoeffs;
    for (auto& f : filt.lp) f.coefficients = filtLpCoeffs;
    for (auto& f : filtLp1) f.coefficients = filtLp1Coeffs;
}

void FusionEQAudioProcessor::rebuildAir()
{
    const auto& spec = Fusion::getSpec (getMode());
    const auto stages = juce::jlimit (1, (int) maxAirStages, spec.airStages);

    airCoeffs.clear();
    airCoeffs.reserve ((size_t) stages);

    for (int i = 0; i < stages; ++i)
        airCoeffs.push_back (Coeffs::makeHighPass (currentSampleRate, Fusion::airFreq, spec.airQ));

    for (int i = 0; i < (int) airHP.size(); ++i)
        for (auto& f : airHP[(size_t) i])
            f.coefficients = airCoeffs[(size_t) juce::jmin (i, stages - 1)];
}

/* Pultec EQP-1A：boost shelf 与 atten shelf 独立，可同时作用（low-end trick）。
   atten 转折点故意偏移（低频更低、高频更高），叠加时形成 EQP-1A 标志性的
   "峰 + 浅谷"曲线。增益刻度仿原机：0-10 拨盘 ≈ 0 到 ±13~16 dB。 */
void FusionEQAudioProcessor::rebuildPultec()
{
    static constexpr float cps[4]  = { 20.0f, 30.0f, 60.0f, 100.0f };
    static constexpr float kcs[5]  = { 5000.0f, 8000.0f, 10000.0f, 12000.0f, 16000.0f };
    static constexpr float attenK[3] = { 5000.0f, 10000.0f, 20000.0f };   // ATTEN SEL 5/10/20 KC

    const int  lfi  = juce::jlimit (0, 3, plLowFreqI);
    const int  hfi  = juce::jlimit (0, 4, plHighFreqI);
    const float bandV = juce::jlimit (0.0f, 1.0f, plBandV);   // 0=BROAD 1=SHARP 线性插值

    const float lb = juce::jlimit (0.0f, 10.0f, plLowBoostV);
    const float la = juce::jlimit (0.0f, 10.0f, plLowAttenV);
    const float hb = juce::jlimit (0.0f, 10.0f, plHighBoostV);

    const float lbDb = lb * 1.4f;                    // 满程 +14 dB
    const float laDb = la * 1.1f;                    // 满程 -11 dB
    const float hbDb = hb * 1.4f;
    const float haDb = juce::jlimit (0.0f, 10.0f, plHighAttenAmtV) * 1.6f;   // 满程 -16 dB
    const int   hai  = juce::jlimit (0, 2, plHighAttenI);                    // 转折频率 5/10/20 KC

    const float lbQ = 0.55f + 0.09f * lb;            // boost 越大峰越突出
    const float hbQ = 0.5f + 0.7f * bandV;

    plCoeffs[(size_t) 0] = Coeffs::makeLowShelf (currentSampleRate, cps[lfi], lbQ,
                                                 juce::Decibels::decibelsToGain (lbDb));
    plCoeffs[(size_t) 1] = Coeffs::makeLowShelf (currentSampleRate, cps[lfi] * 0.75f, 0.707f,
                                                 juce::Decibels::decibelsToGain (-laDb));
    plCoeffs[(size_t) 2] = Coeffs::makeHighShelf (currentSampleRate, kcs[hfi], hbQ,
                                                  juce::Decibels::decibelsToGain (hbDb));
    plCoeffs[(size_t) 3] = Coeffs::makeHighShelf (currentSampleRate, attenK[hai], 0.707f,
                                                  juce::Decibels::decibelsToGain (-haDb));

    for (int k = 0; k < 4; ++k)
    {
        auto* shelfArr = (k == 0) ? &plLowBoostF : (k == 1) ? &plLowAttenF
                        : (k == 2) ? &plHighBoostF : &plHighAttenF;
        for (auto& f : shelfArr->ch)
            f.coefficients = plCoeffs[(size_t) k];
    }
}

/* ============================ Q 计算 ============================ */
float FusionEQAudioProcessor::effectiveQ (const BandState& b) const noexcept
{
    const auto& spec = Fusion::getSpec (getMode());

    /* Manley Massive Passive：Q 按原机固定特性，不开放 ——
       钟形段保留被动网络的"增益越大略窄"手感（约 0.55→1.0），
       搁架段斜率固定（带宽钮在原机也影响搁架，此处取中庸值） */
    if (getMode() == Fusion::Mode::Manley)
    {
        if (b.type != (int) Fusion::BandType::Bell)
            return 0.62f;

        const auto g = std::abs (b.gain.getTargetValue());
        return juce::jlimit (0.55f, 1.0f, 0.55f + 0.45f * g / 20.0f);
    }

    if (spec.proportionalQ)
    {
        /* API 比例 Q：增益越大 → Q 越高（带宽越窄）；小增益时极宽 */
        const auto g = std::abs (b.gain.getTargetValue());
        return juce::jlimit (0.05f, 24.0f, b.q.getTargetValue() * (0.36f + 0.065f * g));
    }

    return juce::jlimit (0.05f, 24.0f, b.q.getTargetValue() * spec.qMul);
}

/* JUCE 的 makeBandPass 中心峰值增益恒为 ≈0.5（-6 dB）。
   乘 2 归一化到 1.0（0 dB），与其它类型保持一致的增益预期。 */
static Coeffs::Ptr makeUnitBandPass (double sr, float f, float q)
{
    auto a = juce::dsp::IIR::ArrayCoefficients<float>::makeBandPass (sr, f, q);
    a[0] *= 2.0f;
    a[1] *= 2.0f;
    a[2] *= 2.0f;
    return *new Coeffs (a);
}

/* 一阶平直倾斜（Flat Tilt / Tilt Shelf）：
   H(s) = kr · (s + w0/kr) / (s + w0·kr)
   以 freq 为支点（该频点增益恒为 0 dB），低频端 -G/2 dB、高频端 +G/2 dB，
   全程斜率平直过渡，不产生搁架的拐角。G 由 GAIN 旋钮决定。 */
static Coeffs::Ptr makeTiltShelf (double sr, float f, float gainDb)
{
    const float T  = 1.0f / (float) sr;
    const float w0 = juce::MathConstants<float>::twoPi * f;
    const float kr = std::pow (10.0f, gainDb / 40.0f);   /* 两端 ±G/2 dB */
    const float A  = kr;
    const float wz = w0 / kr, wp = w0 * kr;
    const float b0 = A * (2.0f / T + wz);
    const float b1 = -A * (2.0f / T);
    const float a0 = (2.0f / T + wp);
    const float a1 = -(2.0f / T);
    return *new Coeffs (b0 / a0, b1 / a0, 0.0f, 1.0f, a1 / a0, 0.0f);
}

/* ============================ 系数更新 ============================ */
void FusionEQAudioProcessor::updateBandCoefficients (int bandIndex)
{
    auto& b = bands[(size_t) bandIndex];
    const auto sr = currentSampleRate;
    const auto f  = juce::jlimit (Fusion::fMin, Fusion::fMax, b.freq.getTargetValue());
    const auto qUser = effectiveQ (b);
    const auto g  = b.on ? b.gain.getTargetValue() : 0.0f;
    const auto gl = juce::Decibels::decibelsToGain (g);

    /* --- 低切 / 高切：先把斜率分节算出来，它决定主节用哪个 Q ---
       关键：纯 Butterworth 要求所有节都来自**同一行梯形**。若第 0 节直接用
       用户的 Q（默认 0.707），而其余节取梯形值（1.3066 / 1.9319 / 5.10…），
       级联就不再平直 —— 截止点上方会鼓出 +4~+6 dB 的包（实测 24 dB/oct
       +4.66 dB，正是用户看到的"低频隆起"）。
       所以把用户 Q 解释为"相对平直值的比例"：
           qMain = 梯形首节 × (用户Q / 0.7071)
       默认 Q=0.7071 → 比例 1 → 每档都精确平直；用户往上拉才故意出共振。 */
    const bool isCut = b.type == (int) Fusion::BandType::LowCut
                    || b.type == (int) Fusion::BandType::HighCut;

    int  sec2 = 0;
    bool has1 = false;
    int  units = 2;      // 截止段总阶数 N = 斜率/6（非截止段占位不用）
    float fc   = f;      // 精确 N 阶 Butterworth：-3 dB 点即截止频率，无需补偿

    if (isCut)
    {
        units = juce::jlimit (1, 32,
                        (int) std::round (juce::jlimit (6.0f, 192.0f, b.slopeDb) / 6.0f));

        sec2 = units / 2;          // 二阶级数（含主节）
        has1 = (units % 2) != 0;   // 还差一个 6 dB/oct 的一阶节
    }

    /* 档位 / 类型变化：清掉斜率各级（含一阶节）的历史状态。
       一阶节在关闭期间一直保留旧状态，下次启用时用陈旧状态起步 = 爆音；
       刚启用的二阶级同理（带着上一档的残留状态）。 */
    if (b.slopeBuiltSec2 != sec2 || b.slopeBuiltHas1st != has1
         || b.slopeBuiltType != b.type)
    {
        for (auto& row : b.slopeFilters)
            for (auto& ff : row)
                ff.reset();

        for (auto& op : b.slopeFirst)
            op.reset();

        b.slopeBuiltSec2   = sec2;
        b.slopeBuiltHas1st = has1;
        b.slopeBuiltType   = b.type;
    }

    const auto q = (isCut && sec2 > 0) ? cutSlopeQ (units, 0) * (qUser / 0.7071f)
                                       : qUser;

    Coeffs::Ptr c;

    switch ((Fusion::BandType) b.type)
    {
        case Fusion::BandType::LowShelf:  c = Coeffs::makeLowShelf   (sr, f, q, gl); break;
        case Fusion::BandType::HighShelf: c = Coeffs::makeHighShelf   (sr, f, q, gl); break;
        case Fusion::BandType::Notch:     c = Coeffs::makeNotch      (sr, f, q);      break;
        case Fusion::BandType::BandPass:  c = makeUnitBandPass       (sr, f, q);      break;
        case Fusion::BandType::LowCut:    c = Coeffs::makeHighPass   (sr, fc, q);     break;
        case Fusion::BandType::HighCut:   c = Coeffs::makeLowPass    (sr, fc, q);     break;
        case Fusion::BandType::TiltShelf: c = makeTiltShelf          (sr, f, g);      break;
        case Fusion::BandType::AllPass:   c = Coeffs::makeAllPass    (sr, f, q);      break;
        case Fusion::BandType::Bell:
        default:                          c = Coeffs::makePeakFilter (sr, f, q, gl); break;
    }

    bandCoeffs[(size_t) bandIndex] = c;

    for (auto& f : b.filters)
        f.coefficients = c;

    /* 动态压缩检测支路（干净模式）：同一组系数的独立实例 */
    for (auto& f : b.detFilters)
        f.coefficients = c;

    /* --- 低切 / 高切段斜率：离散档位（见 Defs.h cutSlopeSteps）---
       附加二阶节用 Butterworth 梯形 Q（cutSlopeQ(units, s)）；6 的奇数档
       再补一个一阶节（6 dB/oct，承担奇数阶的实极点）。
       所有节截止频率一致（fc = 频点）：拼出来即精确 N 阶 Butterworth。
       6 dB/oct 档没有二阶节 —— 主节系数置恒等旁通，只留一阶节。
       （主节 Q 已在上面按"梯形首节 × 用户比例"算好。） */
    b.slopeSec2   = sec2;
    b.slopeHas1st = has1;

    if (isCut)
    {
        const bool isHp = (b.type == (int) Fusion::BandType::LowCut);

        if (b.slopeSec2 == 0)
        {
            /* 只有一阶节：主节置恒等（b0=1, a0=1, 其余 0）＝ 旁通 */
            Coeffs::Ptr ident (new Coeffs (1.0f, 0.0f, 0.0f,
                                           1.0f, 0.0f, 0.0f));
            bandCoeffs[(size_t) bandIndex] = ident;

            for (auto& ff : b.filters)       ff.coefficients = ident;
            for (auto& ff : b.detFilters)    ff.coefficients = ident;
        }

        for (int s = 1; s < b.slopeSec2; ++s)
        {
            auto cs = isHp ? Coeffs::makeHighPass (sr, fc, cutSlopeQ (units, s))
                           : Coeffs::makeLowPass  (sr, fc, cutSlopeQ (units, s));

            for (auto& ff : b.slopeFilters[(size_t) (s - 1)])
                ff.coefficients = cs;
        }

        if (b.slopeHas1st)
            for (auto& op : b.slopeFirst)
                op.setCutoff ((double) fc, sr);
    }
}

/* 主节之后接着跑该段的斜率附加节（附加二阶节 + 可选的一阶节）。
   6 dB/oct 档主节是恒等旁通，实际只经这一阶。 */
void FusionEQAudioProcessor::runBandSlope (int bandIndex, int ch,
                                           float* data, int numSamples) noexcept
{
    auto& b = bands[(size_t) bandIndex];

    for (int s = 1; s < b.slopeSec2; ++s)
        runFilter (b.slopeFilters[(size_t) (s - 1)][(size_t) ch], data, numSamples);

    if (b.slopeHas1st)
    {
        auto& op = b.slopeFirst[(size_t) ch];

        for (int i = 0; i < numSamples; ++i)
            data[i] = op.processSample (data[i]);
    }
}

void FusionEQAudioProcessor::updateTiltCoefficients()
{
    const auto& spec = Fusion::getSpec (getMode());
    const auto sr = currentSampleRate;

    const Fusion::TiltDef defs[3] { spec.tiltLow, spec.tiltMid, spec.tiltHigh };

    for (int i = 0; i < 3; ++i)
    {
        const auto& d  = defs[i];
        const auto gl = juce::Decibels::decibelsToGain (d.gainDb);

        Coeffs::Ptr c;
        if      (i == 0) c = Coeffs::makeLowShelf   (sr, d.freq, d.q, gl);
        else if (i == 2) c = Coeffs::makeHighShelf  (sr, d.freq, d.q, gl);
        else             c = Coeffs::makePeakFilter (sr, d.freq, d.q, gl);

        tiltCoeffs[(size_t) i] = c;

        for (auto& f : tilt[(size_t) i].f)
            f.coefficients = c;
    }
}

/* ============================ 主处理 ============================ */
void FusionEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numChannels = juce::jmin (2, buffer.getNumChannels());
    const auto numSamples  = buffer.getNumSamples();

    if (numChannels <= 0 || numSamples <= 0)
        return;

    /* 旁路：直通，但仍喂频谱，方便对照干/湿 */
    if (bypassed.load())
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            float peak = 0.0f;
            for (int i = 0; i < numSamples; ++i)
                peak = juce::jmax (peak, std::abs (data[i]));

            if (ch == 0) outL.store (peak);
            else          outR.store (peak);
        }
        pushSamplesForAnalysis (buffer);
        return;
    }

    const auto mode = getMode();
    const auto& spec = Fusion::getSpec (mode);

    /* 模式切换：只改染色 / 低切手感 / Q 行为，保留用户段参数（便于 A/B 对比） */
    if (mode != lastMode)
    {
        lastMode = mode;
        rebuildHpf();
        rebuildAir();
        rebuildPultec();
        updateTiltCoefficients();
        for (auto& b : bands)
            b.dirty = true;
        for (auto& c : color)
            c.setMode (spec.shaper, spec.amount, spec.drive, spec.makeup);
        responsePending.store (true);
    }

    /* 低切开关 */
    const bool newHpfOn = apvts.getRawParameterValue (IDs::hpfOn)->load() > 0.5f;
    if (newHpfOn != hpfOn)
    {
        hpfOn = newHpfOn;
        responsePending.store (true);
    }

    /* 硬朗 215L：LO-PASS 独立开关 */
    const bool newLpOn = apvts.getRawParameterValue (IDs::lpOn)->load() > 0.5f;
    if (newLpOn != lpOn)
    {
        lpOn = newLpOn;
        responsePending.store (true);
    }

    /* 低切斜率：0=12 1=24 2=36 dB/oct → 级联级数 1/2/3
       （各级 Q 依赖阶数，斜率变化时需重建系数） */
    const int newSlope = juce::jlimit (0, 2, juce::roundToInt (
        apvts.getRawParameterValue (IDs::hpfSlope)->load()));
    hpfStages = 1 + newSlope;

    /* 低切频点：步进选择 50/80/160/300 Hz */
    {
        const int fIdx = juce::jlimit (0, 3, juce::roundToInt (
            apvts.getRawParameterValue (IDs::hpfFreq)->load()));
        constexpr float freqSteps[4] { 50.0f, 80.0f, 160.0f, 300.0f };

        if (fIdx != lastHpfFreq)
        {
            lastHpfFreq  = fIdx;
            hpfFreqHz    = freqSteps[fIdx];
            rebuildHpf();
            responsePending.store (true);
        }
    }

    if (newSlope != lastHpfSlope)
    {
        lastHpfSlope = newSlope;
        rebuildHpf();
        responsePending.store (true);
    }

    /* SSL 滤波频点（仅 SSL 模式接入信号链） */
    {
        const float nHp = apvts.getRawParameterValue (IDs::filtHp)->load();
        const float nLp = apvts.getRawParameterValue (IDs::filtLp)->load();

        if (std::abs (nHp - filtHpHz) > 1.0e-4f || std::abs (nLp - filtLpHz) > 1.0e-4f)
        {
            filtHpHz = nHp;
            filtLpHz = nLp;
            rebuildFilt();
            responsePending.store (true);
        }
    }

    /* AIR */
    const bool  newAirOn  = apvts.getRawParameterValue (IDs::airOn)->load()  > 0.5f;
    const float newAirAmt = apvts.getRawParameterValue (IDs::airAmt)->load();

    if (std::abs (newAirAmt - airAmt) > 1.0e-6f || newAirOn != airOn)
    {
        airAmt = newAirAmt;
        airOn  = newAirOn;
        airGain.setTargetValue (airOn ? Fusion::airLinear (airAmt) : 0.0f);
        responsePending.store (true);
    }

    /* Pultec 三段（温润模式）：任何相关参数变化都重建 4 组 shelf 系数 */
    {
        const float nLB = apvts.getRawParameterValue (IDs::plLowBoost)->load();
        const float nLA = apvts.getRawParameterValue (IDs::plLowAtten)->load();
        const float nHB = apvts.getRawParameterValue (IDs::plHighBoost)->load();
        const int   nHA = juce::roundToInt (apvts.getRawParameterValue (IDs::plHighAtten)->load());
        const float nHAA = apvts.getRawParameterValue (IDs::plHighAttenAmt)->load();
        const int   nLF = juce::roundToInt (apvts.getRawParameterValue (IDs::plLowFreq)->load());
        const int   nHF = juce::roundToInt (apvts.getRawParameterValue (IDs::plHighFreq)->load());
        const float nBD = apvts.getRawParameterValue (IDs::plBand)->load();

        if (std::abs (nLB - plLowBoostV) > 1.0e-4f || std::abs (nLA - plLowAttenV) > 1.0e-4f
         || std::abs (nHB - plHighBoostV) > 1.0e-4f
         || nHA != plHighAttenI || std::abs (nHAA - plHighAttenAmtV) > 1.0e-4f
         || nLF != plLowFreqI || nHF != plHighFreqI || std::abs (nBD - plBandV) > 1.0e-4f)
        {
            plLowBoostV = nLB;  plLowAttenV = nLA;
            plHighBoostV = nHB;
            plHighAttenI = nHA;   plHighAttenAmtV = nHAA;
            plLowFreqI = nLF;   plHighFreqI = nHF;  plBandV = nBD;
            rebuildPultec();
            responsePending.store (true);
        }
    }

    /* 输出增益 / 自动增益补偿 */
    outGain.setTargetValue (juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue (IDs::output)->load()));

    autoGainOn = apvts.getRawParameterValue (IDs::autoGain)->load() > 0.5f;

    /* 过采样倍率：切换时重建半带滤波器并更新向宿主声明的延迟 */
    const int newOsPow = juce::jlimit (0, 4, juce::roundToInt (
        apvts.getRawParameterValue (IDs::osMode)->load()));

    if (newOsPow != osPow)
    {
        osPow = newOsPow;

        for (auto& c : color)
            c.setOversamplePower (osPow);

        setLatencySamples (color[0].getLatencySamples());
    }

    /* 段参数 */
    bool anyDirty = false;

    for (int i = 0; i < Fusion::numBands; ++i)
    {
        auto& b = bands[(size_t) i];

        const auto newType = juce::jlimit (0, (int) Fusion::BandType::Count - 1,
            (int) std::round (apvts.getRawParameterValue (IDs::bType (i))->load()));
        const auto newFreq = apvts.getRawParameterValue (IDs::bFreq (i))->load();
        const auto newGain = apvts.getRawParameterValue (IDs::bGain (i))->load();
        auto newQ          = apvts.getRawParameterValue (IDs::bQ    (i))->load();
        const auto newSlope = juce::jlimit (6.0f, 192.0f,
                                            apvts.getRawParameterValue (IDs::bSlope (i))->load());

        if (std::abs (b.slopeDb - newSlope) > 1.0e-3f)
        {
            b.slopeDb = newSlope;
            b.dirty   = true;
        }

        /* NEVE 1073：Q 由硬件电路固定，不开放 —— LOW/HIGH 谢尔夫 0.707、MID 钟形 0.70。
           只覆盖音频链使用的 Q，参数本身不动（其它模式的 Q 值不受影响）。 */
        if (mode == Fusion::Mode::Neve)
            newQ = (i == 2 ? 0.70f : 0.7071f);

        /* API 550L：Q 不开放 —— 谢尔夫段固定 0.707；钟形段用 API 招牌的比例 Q
           （增益越大带宽越窄、小增益极宽），随该段增益实时变化。 */
        if (mode == Fusion::Mode::API)
            newQ = (i == 0 || i == 5)
                       ? 0.7071f
                       : juce::jlimit (0.70f, 1.30f,
                                       0.70f + 0.60f * std::abs (newGain) / 12.0f);

        /* SSL 4000E 4 段模式：被屏蔽的频段强制视为 OFF（隐藏且旁路） */
        const bool newOn   = Fusion::isBandActive (mode, i)
                             && apvts.getRawParameterValue (IDs::bOn (i))->load() > 0.5f;

        if (b.type != newType || b.on != newOn)
        {
            b.type = newType;
            b.on   = newOn;
            b.dirty = true;
        }
        if (std::abs (b.freq.getTargetValue() - newFreq) > 1.0e-4f)
        {
            b.freq.setTargetValue (newFreq);
            b.dirty = true;
        }
        if (std::abs (b.gain.getTargetValue() - newGain) > 1.0e-4f)
        {
            b.gain.setTargetValue (newGain);
            b.dirty = true;
        }
        if (std::abs (b.q.getTargetValue() - newQ) > 1.0e-4f)
        {
            b.q.setTargetValue (newQ);
            b.dirty = true;
        }

        if (b.dirty)
        {
            updateBandCoefficients (i);
            b.dirty = false;
            anyDirty = true;
        }

        /* 动态压缩参数（F6 式，仅干净模式读取；其它模式强制关闭） */
        if (mode == Fusion::Mode::Clean)
        {
            b.dynOn  = apvts.getRawParameterValue (IDs::bDynOn (i))->load() > 0.5f;
            b.dynThr = apvts.getRawParameterValue (IDs::bDynThr (i))->load();
            b.dynRng = apvts.getRawParameterValue (IDs::bDynRng (i))->load();
            b.dynAtt = apvts.getRawParameterValue (IDs::bDynAtt (i))->load();
            b.dynRel = apvts.getRawParameterValue (IDs::bDynRel (i))->load();
            /* Mono 轨（主输入 < 2 声道）没有立体声拓扑：
               Left / Right / Mid / Side 全部降级成 Stereo 处理。
               这里统一降级，下游主链路由 / 检测 / delta 三条路径就天然一致，
               不会出现在 Mono 上把 Side 段算成静音、或两处算重导致杂音的情况。 */
            b.chan   = (numChannels >= 2)
                     ? (int) apvts.getRawParameterValue (IDs::bChan (i))->load()
                     : 0;
        }
        else
        {
            b.dynOn = false;
            b.chan  = 0;
        }
    }

    if (anyDirty)
        responsePending.store (true);

    /* 频响重算很贵（砖墙档 16 节 = 16 次系数堆分配 + 16×curvePoints 幅频）。
       滚轮连滚时每块都做 → 音频线程过载（爆音）且抢锁把 UI 也卡住（一卡一卡）。
       这里给音频线程限频到 ~16 Hz，其余交给 UI 线程的 refreshCurveFromUI()
       （频谱屏本来就在跑），显示完全跟手，音频线程也不再被拖垮。 */
    if (responsePending.exchange (false))
    {
        const auto now = juce::Time::getMillisecondCounter();

        if (now - lastSnapshotMs >= 60)
        {
            if (tryUpdateResponseSnapshot())
                lastSnapshotMs = now;
            else
                responsePending.store (true);
        }
        else
        {
            responsePending.store (true);   // 还没到点，下个块再试
        }
    }

    /* ---------- 频点监听（SOLO LISTEN）系数 ----------
       监听 = 主链全部处理完之后再过该频段的单位增益带通，
       所以 EQ 的增益/衰减、类型切换在监听里照常可闻，
       拖频点/滚轮 Q 实时改变带通位置和宽度（扫频）。 */
    const int lb = listenBand.load();

    if (lb >= 0)
    {
        const auto& lbState = bands[(size_t) lb];
        const auto lf = juce::jlimit (Fusion::fMin, Fusion::fMax, lbState.freq.getTargetValue());
        const auto lq = effectiveQ (lbState);

        if (lb != lastListenBand || lf != lastListenF || lq != lastListenQ)
        {
            auto c = makeUnitBandPass (currentSampleRate, lf, lq);

            for (auto& filter : listenFilters)
                filter.coefficients = c;

            lastListenBand = lb; lastListenF = lf; lastListenQ = lq;
        }
    }

    /* ---------- 干净模式动态 EQ：逐段增益计算机（F6 式，立体声联动） ----------
       检测信号 = 该段系数（共享主链系数的独立实例）滤波后的节目内容；
       峰值包络 → 阈值比较 → 超出部分按 Range 封顶衰减，
       Attack/Release 一阶平滑作用在增益曲线上。 */
    const bool cleanDyn = (mode == Fusion::Mode::Clean);
    bool anyDynOn = false;
    bool anyParallel = false;    // 动态段 + 非 Stereo 声道段（含静态）走并联 delta 路由

    if (cleanDyn)
        for (int i = 0; i < Fusion::numBands; ++i)
        {
            const auto& bs = bands[(size_t) i];

            if (! bs.on || ! ((spec.bandMask >> (juce::uint32) i) & 1u))
                continue;

            if (bs.dynOn)                 anyDynOn    = true;
            if (bs.dynOn || bs.chan != 0) anyParallel = true;
        }

    if (anyDynOn)
    {
        for (auto& gc : dynGrCurve)
            if ((int) gc.size() < numSamples)
                gc.assign ((size_t) numSamples, 1.0f);

        for (int i = 0; i < Fusion::numBands; ++i)
        {
            auto& b = bands[(size_t) i];

            if (! b.on || ! b.dynOn || ! ((spec.bandMask >> (juce::uint32) i) & 1u))
                continue;

            /* 检测支路：原始输入 → 该段带通（双通道） */
            for (int ch2 = 0; ch2 < numChannels; ++ch2)
            {
                juce::FloatVectorOperations::copy (dynDet.getWritePointer (ch2),
                                                   buffer.getReadPointer (ch2), numSamples);
                runFilter (b.detFilters[(size_t) ch2],
                           dynDet.getWritePointer (ch2), numSamples);
            }

            const auto* dL = dynDet.getReadPointer (0);
            const auto* dR = numChannels > 1 ? dynDet.getReadPointer (1) : dL;

            const float sr    = (float) currentSampleRate;
            const float atkCo = std::exp (-1.0f / juce::jmax (1.0f, 0.001f * b.dynAtt * sr));
            const float relCo = std::exp (-1.0f / juce::jmax (1.0f, 0.001f * b.dynRel * sr));
            const float envCo = std::exp (-1.0f / (0.025f * sr));   // 峰值包络 25 ms 回落

            float env = b.env, gr = b.grDb;
            float grMax = 0.0f;

            for (int n = 0; n < numSamples; ++n)
            {
                /* 检测信号跟随声道模式（检测滤波是线性的，
                   Mid/Side 用滤波后的 L/R 线性组合完全等价） */
                const float x = b.chan == 1 ? std::abs (dL[n])
                              : b.chan == 2 ? std::abs (dR[n])
                              : b.chan == 3 ? std::abs (0.5f * (dL[n] + dR[n]))
                              : b.chan == 4 ? std::abs (0.5f * (dL[n] - dR[n]))
                                            : juce::jmax (std::abs (dL[n]), std::abs (dR[n]));

                env = x > env ? x : env * envCo;

                const float levelDb = juce::Decibels::gainToDecibels (env, -80.0f);
                const float over    = levelDb - b.dynThr;
                const float target  = over > 0.0f ? -juce::jmin (b.dynRng, over) : 0.0f;

                gr += (target - gr) * (target < gr ? atkCo : relCo);

                dynGrCurve[(size_t) i][(size_t) n] = juce::Decibels::decibelsToGain (gr);
                grMax = juce::jmin (grMax, gr);
            }

            b.env  = env;
            b.grDb = gr;
            dynGrPub[(size_t) i].store (gr);
        }
    }
    else
        for (int i = 0; i < Fusion::numBands; ++i)
            if (bands[(size_t) i].grDb < -0.05f || dynGrPub[(size_t) i].load() < -0.05f)
                dynGrPub[(size_t) i].store (0.0f);

    /* 并联 delta 路由缓冲（动态段 + 非 Stereo 声道段）：
       只要存在非 Stereo 段（anyParallel）就必须分配 grOneCurve / dynLate，
       否则静态声道段在 processBlock 里访问 gr[n] 会读到空向量（.data()==nullptr）越界崩溃。
       这一分配原先误放在 if(anyDynOn) 内，导致"无动态段、仅设 Mid/Side"时崩宿主。 */
    if (cleanDyn && anyParallel)
    {
        /* 兜底：宿主偶尔给出超过 prepare 声明的块长（某些宿主/渲染离线），
           环形装不下会导致块内自覆盖 —— 此时就地扩容，宁可多占内存也不能出杂音 */
        if (numSamples > laSize)
        {
            laSize = numSamples;
            laBuf.assign ((size_t) Fusion::numBands * 2u * (size_t) laSize, 0.0f);
            laPos = 0;
        }

        if ((int) grOneCurve.size() < numSamples)
            grOneCurve.assign ((size_t) numSamples, 1.0f);

        for (auto& dl : dynLate)
            if ((int) dl.size() < numSamples)
                dl.assign ((size_t) numSamples, 0.0f);
    }

    /* ---------- 处理：主链 + AIR 混合（此处先不加输出增益） ---------- */
    const int nStages = (int) airCoeffs.size();

    /* 并联 delta 路由需要原始输入：一次性拷齐全部声道
       （Mid/Side 在处理 ch0 时就要用到对面声道） */
    if (cleanDyn && anyParallel)
        for (int ch2 = 0; ch2 < numChannels; ++ch2)
            juce::FloatVectorOperations::copy (dynDry.getWritePointer (ch2),
                                               buffer.getReadPointer (ch2), numSamples);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);

        /* --- AIR 平行支路：从输入取，与主链完全独立 --- */
        if (nStages > 0)
        {
            auto* a = airScratch.getWritePointer (ch);
            juce::FloatVectorOperations::copy (a, data, numSamples);

            for (int k = 0; k < nStages; ++k)
                runFilter (airHP[(size_t) k][(size_t) ch], a, numSamples);
        }

        /* --- 主链：Butterworth 级联（斜率）+ 性格节（模式质感） ---
           SSL 模式走原版 4000 滤波（HP/LP 固定 12 dB/oct，端点 = OUT 旁通）；
           Pultec 三段式无低切（原机无此功能） */
        if (getMode() == Fusion::Mode::SSL)
        {
            if (filtHpHz > Fusion::fMin + 0.5f)        // HP 拧到最左 = OUT
                runFilter (filt.hp[(size_t) ch], data, numSamples);

            if (filtLpHz < 19995.0f)                   // LP 拧到最右 = OUT
                runFilter (filt.lp[(size_t) ch], data, numSamples);
        }
        else if (getMode() == Fusion::Mode::API)
        {
            /* 硬朗 215L：双滤波复用同一对系数（12 dB/oct Butterworth），
               HP / LP 开关独立通断（面板按钮，也可宿主自动化）；
               所有档位都是有效频点（无 OUT 端点） */
            if (hpfOn)
                runFilter (filt.hp[(size_t) ch], data, numSamples);
            if (lpOn)
                runFilter (filt.lp[(size_t) ch], data, numSamples);
        }
        else if (getMode() == Fusion::Mode::Manley)
        {
            /* Manley Massive Passive 高/低切：全部无源，OFF = 旋钮端点；
               HIGH PASS 12 dB/oct，LOW PASS 原机 18 dB/oct（二阶 + 一阶） */
            if (filtHpHz > 20.5f)
                runFilter (filt.hp[(size_t) ch], data, numSamples);

            if (filtLpHz < 19995.0f)
            {
                runFilter (filt.lp [(size_t) ch], data, numSamples);
                runFilter (filtLp1[(size_t) ch], data, numSamples);
            }
        }
        else if (hpfOn && getMode() != Fusion::Mode::Pultec)
        {
            for (int s = 0; s < hpfStages; ++s)
                runFilter (hpf.biquad[(size_t) s][(size_t) ch], data, numSamples);

            runFilter (hpf.character[(size_t) ch], data, numSamples);
        }

        if (getMode() == Fusion::Mode::Manley)
        {
            /* --- Massive Passive 灵魂：并联无源拓扑 ---
               各段并联相加：out = in + Σ(段输出 − in)。四段同频提升不会像
               串联 EQ 那样叠出 80 dB，只会温和地累加 —— 这是"Massive"手感的来源。
               OUT 的段（bOn=false）系数为单位增益，贡献恰好为零 */
            auto* sum  = mpSum .getWritePointer (ch);
            auto* work = mpBand.getWritePointer (ch);

            juce::FloatVectorOperations::copy (sum, data, numSamples);

            int bi = 0;
            for (auto& b : bands)
            {
                const bool act = (spec.bandMask >> (juce::uint32) bi++) & 1u;
                if (! act) continue;

                juce::FloatVectorOperations::copy (work, data, numSamples);
                runFilter (b.filters[(size_t) ch], work, numSamples);
                runBandSlope (bi - 1, ch, work, numSamples);   // bi 已自增，回退到本段索引

                for (int i = 0; i < numSamples; ++i)
                    sum[i] += work[i] - data[i];
            }

            juce::FloatVectorOperations::copy (data, sum, numSamples);
        }
        else
        {
            int bi = 0;

            for (auto& b : bands)
            {
                const bool act = (spec.bandMask >> (juce::uint32) bi++) & 1u;

                /* 干净模式：动态段 + 非 Stereo 声道段走并联（下面叠加），
                   其余静态段原样串联 */
                if (act && ! (cleanDyn && (b.dynOn || b.chan != 0)))
                    runFilter (b.filters[(size_t) ch], data, numSamples);
                    runBandSlope (bi - 1, ch, data, numSamples);
            }
        }

        /* --- 干净模式并联段贡献 out += g(t) · (段输出 − 输入) ---
           覆盖两类段：动态段（g = GR 增益曲线）与
           非 Stereo 声道段（g = 1，Left/Right/Mid/Side 路由）。
           段外频率 delta≈0，增益衰减只作用于该段附近 —— 这是动态 EQ
           不会把整条频谱一起压下去的关键。
           Look-ahead（混音音质优先）：动态段的 delta 先写进 10 ms 环形缓冲、
           取出的是 10 ms 之前的 delta —— 增益曲线因此比信号提前 10 ms
           作用，压缩先于瞬态发生，干净无抽吸（内部补偿，无附加延迟）。
           静态特殊声道段不走环形缓冲（无增益曲线，延迟反而造成梳状失真）。 */
        if (cleanDyn && anyParallel)
        {
            const auto* dryL = dynDry.getReadPointer (0);
            const auto* dryR = numChannels > 1 ? dynDry.getReadPointer (1) : dryL;
            const auto* dry  = dynDry.getReadPointer (ch);
            auto*       work = dynBand.getWritePointer (ch);
            const int   pos0 = laPos;

            /* ch0 会把所有 Mid/Side 段的另一声道贡献累加到 lateR，
               所以每块 ch0 处理前先把 lateR 清零，ch1 再统一取用。 */
            if (ch == 0)
                for (auto& dl : dynLate)
                    std::fill (dl.begin(), dl.end(), 0.0f);

            for (int bi = 0; bi < Fusion::numBands; ++bi)
            {
                auto& b = bands[(size_t) bi];

                if (! ((spec.bandMask >> (juce::uint32) bi) & 1u) || ! b.on)
                    continue;

                if (! (b.dynOn || b.chan != 0))
                    continue;

                const float* gr = b.dynOn ? dynGrCurve[(size_t) bi].data()
                                          : grOneCurve.data();
                const int    cm = b.chan;

                /* --- Stereo / Left / Right：各声道独立滤波，delta 入自家环形 --- */
                if (cm <= 2)
                {
                    const bool act = (cm == 0) || (cm == 1 && ch == 0)
                                                  || (cm == 2 && ch == 1);
                    auto* ring = laBuf.data()
                               + ((size_t) bi * 2u + (size_t) ch) * (size_t) laSize;
                    int pos = pos0;

                    if (act)
                    {
                        juce::FloatVectorOperations::copy (work, dry, numSamples);
                        runFilter (b.filters[(size_t) ch], work, numSamples);
                        runBandSlope (bi, ch, work, numSamples);

                        for (int n = 0; n < numSamples; ++n)
                        {
                            const float delta = work[n] - dry[n];
                            float old;

                            if (b.dynOn)  { old = ring[pos]; ring[pos] = delta; }
                            else            old = delta;      // 静态段：相位对齐直加
                            pos = pos + 1 == laSize ? 0 : pos + 1;

                            data[n] += gr[n] * old;
                        }
                    }
                    else if (b.dynOn)
                    {
                        for (int n = 0; n < numSamples; ++n)  // 不活动声道：喂零保持环形同步
                        {
                            ring[pos] = 0.0f;
                            pos = pos + 1 == laSize ? 0 : pos + 1;
                        }
                    }

                    continue;
                }

                /* --- Mid / Side：ch0 用 (L±R)/2 过单只滤波器 ---
                   Mid：delta 同相加到两声道；Side：L 加 R 减。
                   ch1 的贡献由 ch0 现算进 scratch（不走环形 ——
                   ch1 在 ch0 之后处理，读环形会读到刚写入的新值） */
                if (ch == 0)
                {
                    const float sg = cm == 3 ? 1.0f : -1.0f;

                    for (int n = 0; n < numSamples; ++n)
                        work[n] = sg * 0.5f * (dryL[n] + dryR[n])
                                + (1.0f - sg) * 0.5f * (dryL[n] - dryR[n]);

                    runFilter (b.filters[0], work, numSamples);
                    runBandSlope ((int) bi, 0, work, numSamples);

                    auto* lateR = dynLate[numChannels > 1 ? 1 : 0].data();
                    auto* ring0 = laBuf.data() + ((size_t) bi * 2u) * (size_t) laSize;
                    int pos = pos0;

                    for (int n = 0; n < numSamples; ++n)
                    {
                        const float src = cm == 3 ? 0.5f * (dryL[n] + dryR[n])
                                                  : 0.5f * (dryL[n] - dryR[n]);
                        const float delta = work[n] - src;
                        float old;

                        if (b.dynOn)  { old = ring0[pos]; ring0[pos] = delta; }
                        else            old = delta;
                        pos = pos + 1 == laSize ? 0 : pos + 1;

                        data [n] += gr[n] * old;
                        lateR[n] += gr[n] * sg * old;
                    }
                }
                else
                {
                    const auto* lateR = dynLate[1].data();

                    for (int n = 0; n < numSamples; ++n)
                        data[n] += lateR[n];
                }
            }

            if (ch == numChannels - 1)
            {
                /* 环形位置必须每块推进 numSamples ——
                   原先这里写的是 laPos = pos0（起点从不变动），读到的就只是
                   "上一块同一位置"的值，而不是 10 ms 之前的采样，
                   延迟会在块内线性塌缩，听感就是明显的杂味电流声。 */
                laPos = (pos0 + numSamples) % laSize;
            }
        }

        /* --- Pultec 三段（温润模式专属）：low boost+atten → high boost+atten --- */
        if (getMode() == Fusion::Mode::Pultec)
        {
            runFilter (plLowBoostF.ch[(size_t) ch],  data, numSamples);
            runFilter (plLowAttenF.ch[(size_t) ch],  data, numSamples);
            runFilter (plHighBoostF.ch[(size_t) ch], data, numSamples);
            runFilter (plHighAttenF.ch[(size_t) ch], data, numSamples);
        }

        for (auto& t : tilt)
            runFilter (t.f[(size_t) ch], data, numSamples);

        /* 染色级：preGain -> 过采样 -> 饱和 -> 抽取 -> postGain */
        color[(size_t) ch].processBlock (data, numSamples);

        /* --- 叠加 AIR（不含输出增益，便于测量真实响度） --- */
        auto* a = airScratch.getReadPointer (ch);

        for (int i = 0; i < numSamples; ++i)
            data[i] += airGain.getNextValue() * a[i];

        /* --- 监听（SOLO LISTEN）：主链处理完后再过带通 ---
               听到的是 EQ 处理后的该频段：拖增益/衰减、换类型实时可闻 */
        if (lb >= 0)
            runFilter (listenFilters[(size_t) ch], data, numSamples);
    }

    /* ---------- 自动增益补偿 ----------
       目标值来自频响曲线本身（见 updateResponseSnapshot），与节目电平无关，
       所以推子/音乐起伏不会被它"拉平"，只抵消 EQ 造成的整体响度变化。 */
    {
        const auto targetDb = (autoGainOn && lb < 0) ? autoGainTargetDb.load() : 0.0f;
        const auto target   = juce::Decibels::decibelsToGain (targetDb);

        /* 只在目标变化时才重设斜坡，否则每块都重设会让平滑变慢 */
        if (target != autoGain.getTargetValue())
        {
            autoGain.setTargetValue (target);
            autoGainDb.store (juce::Decibels::gainToDecibels (target));
        }
    }

    /* ---------- 输出增益 + 电平表 ---------- */
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        float peak = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            data[i] *= autoGain.getNextValue() * outGain.getNextValue();
            peak = juce::jmax (peak, std::abs (data[i]));
        }

        if (ch == 0) outL.store (peak);
        else         outR.store (peak);
    }

    pushSamplesForAnalysis (buffer);
}

/* ============================ 频谱分析 ============================ */
void FusionEQAudioProcessor::pushSamplesForAnalysis (const juce::AudioBuffer<float>& buffer)
{
    if (buffer.getNumChannels() <= 0 || fftHistory.getNumSamples() < fftSize)
        return;

    const auto* src = buffer.getReadPointer (0);
    const auto num  = buffer.getNumSamples();
    auto* hist = fftHistory.getWritePointer (0);

    for (int i = 0; i < num; ++i)
    {
        hist[histIndex] = src[i];

        if (++histIndex >= fftSize)
            histIndex = 0;

        /* 每前进 hopSize 个采样就重算一次（75% 重叠）→ 时间分辨率翻了 8 倍 */
        if (++hopCount >= hopSize)
        {
            hopCount = 0;
            runAnalysis();
        }
    }
}

void FusionEQAudioProcessor::runAnalysis()
{
    if (! spectrumMapReady)
        return;

    auto* hist = fftHistory.getReadPointer (0);
    auto* dst  = fftData.data();

    /* 环形历史按时间顺序线性化（写指针之后的是最早的采样） */
    const auto firstLen = fftSize - histIndex;
    std::copy (hist + histIndex, hist + fftSize, dst);
    std::copy (hist, hist + histIndex, dst + firstLen);
    std::fill (dst + fftSize, dst + fftSize * 2, 0.0f);

    window.multiplyWithWindowingTable (dst, (size_t) fftSize);
    forwardFFT.performRealOnlyForwardTransform (dst);

    const int  bins = fftSize / 2;
    const auto norm = 2.0f / (float) fftSize;

    const auto binPower = [dst] (int b)
    {
        const auto re = dst[(size_t) (b * 2)];
        const auto im = dst[(size_t) (b * 2 + 1)];
        return (double) re * re + (double) im * im;
    };

    for (int i = 0; i < spectrumBins; ++i)
    {
        const double pos0 = binPos0[(size_t) i];
        const double pos1 = binPos1[(size_t) i];

        double m = 0.0;

        if (pos1 - pos0 < 0.75)
        {
            /* 低频端：对数频点比 FFT bin 还窄，直接平均会出现阶梯状平台。
               改为在两个相邻 bin 之间按功率线性插值 → 曲线连续。 */
            const auto pos = 0.5 * (pos0 + pos1);
            const int  i0  = juce::jlimit (0, bins - 2, (int) std::floor (pos));
            const auto fr  = juce::jlimit (0.0, 1.0, pos - (double) i0);
            m = std::sqrt (binPower (i0) * (1.0 - fr) + binPower (i0 + 1) * fr) * (double) norm;
        }
        else
        {
            const int b0 = juce::jlimit (0, bins - 1, (int) std::floor (pos0));
            const int b1 = juce::jlimit (b0 + 1, bins, (int) std::ceil (pos1));

            /* 用均方根而不是峰值：高分辨率下更贴近听感，也避免窄带尖刺 */
            double acc = 0.0;
            int    cnt = 0;

            for (int b = b0; b < b1; ++b)
            {
                acc += binPower (b);
                ++cnt;
            }

            m = std::sqrt (acc / (double) juce::jmax (1, cnt)) * (double) norm;
        }

        const auto db = juce::jlimit (-100.0f, 20.0f,
                                      (float) juce::Decibels::gainToDecibels ((float) m + 1.0e-9f));

        /* 时间常数换算（与帧率无关）：上升 12 ms、回落 90 ms。
           分析帧从 12 fps 提到 ~94 fps 后，必须改成按时间算，
           否则同样的系数会让曲线变得过于"黏"。 */
        constexpr float atkTau = 0.012f;
        constexpr float relTau = 0.090f;
        const auto  dt   = (float) hopSize / (float) juce::jmax (1.0, currentSampleRate);
        const auto  prev = specWork[(size_t) i];
        const auto  coef = 1.0f - std::exp (-dt / (db > prev ? atkTau : relTau));

        specWork[(size_t) i] = prev + (db - prev) * coef;
    }

    /* 写入后台缓冲后交换索引 —— seqlock 包裹，界面拷贝读时若撞上写入会重试 */
    const int back = 1 - spectrumIndex.load();
    const auto seq0 = specSeq.load (std::memory_order_relaxed);
    specSeq.store (seq0 + 1u, std::memory_order_relaxed);

    spectrumBuf[(size_t) back] = specWork;
    spectrumIndex.store (back, std::memory_order_release);

    specSeq.store (seq0 + 2u, std::memory_order_release);
}

/* ============================ 快照读取（seqlock 拷贝读） ============================
   界面绘制一条曲线要几百微秒，这期间音频线程可能算完新快照并翻缓冲 ——
   如果界面直接"引用"共享缓冲，就会读到写到一半的新旧混合数据（撕裂帧，
   表现为拖动频点时曲线偶尔"扯一下"）。这里拷贝前后各读一次版本号，
   不一致就重试，读者永远拿到完整一致的快照。 */
bool FusionEQAudioProcessor::readCurve (CurveSnapshot& out) const noexcept
{
    for (int attempt = 0; attempt < 16; ++attempt)
    {
        const auto s1 = curveSeq.load (std::memory_order_acquire);

        if ((s1 & 1u) == 0)                                   // 偶数 = 稳定
        {
            const auto idx = curveIndex.load (std::memory_order_acquire);
            out = curveBuf[(size_t) idx];

            const auto s2 = curveSeq.load (std::memory_order_relaxed);
            if (s1 == s2)
                return true;
        }

        juce::Thread::yield();
    }

    return false;
}

bool FusionEQAudioProcessor::readSpectrum (std::array<float, spectrumBins>& out) const noexcept
{
    for (int attempt = 0; attempt < 16; ++attempt)
    {
        const auto s1 = specSeq.load (std::memory_order_acquire);

        if ((s1 & 1u) == 0)
        {
            const auto idx = spectrumIndex.load (std::memory_order_acquire);
            out = spectrumBuf[(size_t) idx];

            const auto s2 = specSeq.load (std::memory_order_relaxed);
            if (s1 == s2)
                return true;
        }

        juce::Thread::yield();
    }

    return false;
}

/* ============================ 频响快照 ============================
   完全自包含：所有系数都从当前参数值现算，不读音频线程的 bandCoeffs /
   hpfCoeffs / tiltCoeffs —— 所以 UI 线程（停止播放时）也能安全调用。
   曲线只画主链（低切 + 6 段 + 静态倾斜）；AIR 是平行音量混合，
   不属于均衡曲线，不画进频谱。 */
void FusionEQAudioProcessor::updateResponseSnapshot()
{
    const auto sr   = currentSampleRate;
    const auto mode = getMode();
    const auto& m   = Fusion::getSpec (mode);

    const int back = 1 - curveIndex.load();
    auto& snap = curveBuf[(size_t) back];

    /* seqlock：进入写入（奇数版本号），读者此刻开始重试 */
    const auto seq0 = curveSeq.load (std::memory_order_relaxed);
    curveSeq.store (seq0 + 1u, std::memory_order_relaxed);

    auto& freqs = respFreqs;
    auto& tmp   = respTmp;
    auto& tmp2  = respTmp2;
    auto& mag   = respMag;

    for (int i = 0; i < curvePoints; ++i)
    {
        freqs[(size_t) i] = (double) curveFreqs[(size_t) i];
        tmp[(size_t) i]   = 1.0;
        mag[(size_t) i]   = 1.0;
    }

    /* --- 低切：纯 Butterworth 级联（斜率）+ 性格节（模式质感，与斜率无关）
       Pultec 三段式无低切（与音频链一致） --- */
    if (apvts.getRawParameterValue (IDs::hpfOn)->load() > 0.5f
        && mode != Fusion::Mode::Pultec)
    {
        const int stages = 1 + juce::jlimit (0, 2, juce::roundToInt (
            apvts.getRawParameterValue (IDs::hpfSlope)->load()));

        for (int s = 0; s < stages; ++s)
        {
            const auto hpfC = Coeffs::makeHighPass (sr, hpfFreqHz,
                                                    hpfSectionQ (s, stages));
            if (hpfC == nullptr)
                break;

            hpfC->getMagnitudeForFrequencyArray (freqs.data(), tmp.data(),
                                                 (size_t) curvePoints, sr);
            for (int i = 0; i < curvePoints; ++i)
                mag[(size_t) i] *= tmp[(size_t) i];
        }

        if (auto chC = makeHpfCharacter (sr, m.hpfQ, hpfFreqHz))
        {
            chC->getMagnitudeForFrequencyArray (freqs.data(), tmp.data(),
                                                (size_t) curvePoints, sr);
            for (int i = 0; i < curvePoints; ++i)
                mag[(size_t) i] *= tmp[(size_t) i];
        }
    }

    /* --- SSL 滤波：HP + LP（端点 = OUT 旁通，与音频链一致） ---
       硬朗 215L：同一对系数，HP/LP 开关独立门控，全部档位有效 --- */
    if (mode == Fusion::Mode::API)
    {
        if (hpfOn)
            if (auto hpC = Coeffs::makeHighPass (sr, filtHpHz, 0.7071f))
            {
                hpC->getMagnitudeForFrequencyArray (freqs.data(), tmp.data(),
                                                    (size_t) curvePoints, sr);
                for (int i = 0; i < curvePoints; ++i)
                    mag[(size_t) i] *= tmp[(size_t) i];
            }

        if (lpOn)
            if (auto lpC = Coeffs::makeLowPass (sr, filtLpHz, 0.7071f))
            {
                lpC->getMagnitudeForFrequencyArray (freqs.data(), tmp.data(),
                                                    (size_t) curvePoints, sr);
                for (int i = 0; i < curvePoints; ++i)
                    mag[(size_t) i] *= tmp[(size_t) i];
            }
    }
    else if (mode == Fusion::Mode::SSL)
    {
        if (filtHpHz > Fusion::fMin + 0.5f)
            if (auto hpC = Coeffs::makeHighPass (sr, filtHpHz, 0.7071f))
            {
            hpC->getMagnitudeForFrequencyArray (freqs.data(), tmp.data(),
                                                (size_t) curvePoints, sr);
            for (int i = 0; i < curvePoints; ++i)
                mag[(size_t) i] *= tmp[(size_t) i];
        }

        if (filtLpHz < 19995.0f)
            if (auto lpC = Coeffs::makeLowPass (sr, filtLpHz, 0.7071f))
            {
            lpC->getMagnitudeForFrequencyArray (freqs.data(), tmp.data(),
                                                (size_t) curvePoints, sr);
            for (int i = 0; i < curvePoints; ++i)
                mag[(size_t) i] *= tmp[(size_t) i];
        }
    }

    /* --- Manley 高/低切：HP 12 dB/oct；LP 18 dB/oct（二阶 × 一阶）；OFF = 端点 --- */
    if (mode == Fusion::Mode::Manley)
    {
        if (filtHpHz > 20.5f)
            if (auto hpC = Coeffs::makeHighPass (sr, filtHpHz, 0.7071f))
            {
                hpC->getMagnitudeForFrequencyArray (freqs.data(), tmp.data(),
                                                    (size_t) curvePoints, sr);
                for (int i = 0; i < curvePoints; ++i)
                    mag[(size_t) i] *= tmp[(size_t) i];
            }

        if (filtLpHz < 19995.0f)
            if (auto lpC  = Coeffs::makeLowPass (sr, filtLpHz, 0.7071f))
            {
                lpC->getMagnitudeForFrequencyArray (freqs.data(), tmp.data(),
                                                    (size_t) curvePoints, sr);
                for (int i = 0; i < curvePoints; ++i)
                    mag[(size_t) i] *= tmp[(size_t) i];

                if (auto lp1 = Coeffs::makeFirstOrderLowPass (sr, filtLpHz))
                {
                    lp1->getMagnitudeForFrequencyArray (freqs.data(), tmp.data(),
                                                        (size_t) curvePoints, sr);
                    for (int i = 0; i < curvePoints; ++i)
                        mag[(size_t) i] *= tmp[(size_t) i];
                }
            }
    }

    /* --- 各段：直接从参数现算，同时记录每段独立响应 ---
       SSL 4 段模式下被屏蔽的频段直接输出平线，不参与主链乘积；
       Manley 并联拓扑：各段贡献单独累加（out = 1 + Σ(段 − 1)），最后合成 --- */
    std::vector<float> parAcc;

    if (mode == Fusion::Mode::Manley)
        parAcc.assign ((size_t) curvePoints, 0.0f);

    for (int b = 0; b < Fusion::numBands; ++b)
    {
        if (((m.bandMask >> (juce::uint32) b) & 1u) == 0)
        {
            for (int i = 0; i < curvePoints; ++i)
                snap.bandCurves[(size_t) b][(size_t) i] = 0.0f;
            continue;
        }

        const auto type = juce::jlimit (0, (int) Fusion::BandType::Count - 1,
            (int) std::round (apvts.getRawParameterValue (IDs::bType (b))->load()));
        const auto fv = apvts.getRawParameterValue (IDs::bFreq (b))->load();
        const auto gv = apvts.getRawParameterValue (IDs::bGain (b))->load();
        const auto qv = apvts.getRawParameterValue (IDs::bQ    (b))->load();
        const bool on = apvts.getRawParameterValue (IDs::bOn   (b))->load() > 0.5f;

        /* 与 effectiveQ() 相同的 Q 行为 */
        const auto q = (mode == Fusion::Mode::Manley)
            ? ((type == (int) Fusion::BandType::Bell)
                ? juce::jlimit (0.55f, 1.0f, 0.55f + 0.45f * std::abs (gv) / 20.0f)
                : 0.62f)
            : (m.proportionalQ
                ? juce::jlimit (0.05f, 24.0f, qv * (0.36f + 0.065f * std::abs (gv)))
                : juce::jlimit (0.05f, 24.0f, qv * m.qMul));

        const auto f  = juce::jlimit (Fusion::fMin, Fusion::fMax, fv);
        const auto gl = juce::Decibels::decibelsToGain (on ? gv : 0.0f);

        /* 与 updateBandCoefficients 同一套逻辑：低切/高切的主节 Q = 梯形首节 ×
           用户比例（默认比例 1 → 平直），分节数也在这里算出来给后面用 */
        const bool isCut = type == (int) Fusion::BandType::LowCut
                        || type == (int) Fusion::BandType::HighCut;

        int  sec2 = 0;
        bool has1 = false;
        int  units = 2;
        float fc   = f;      // 精确 N 阶 Butterworth：-3 dB 点即截止频率，无需补偿

        if (isCut && on)
        {
            units = juce::jlimit (1, 32,
                            (int) std::round (juce::jlimit (6.0f, 192.0f,
                                            bands[(size_t) b].slopeDb) / 6.0f));

            sec2 = units / 2;
            has1 = (units % 2) != 0;
        }

        const auto qMain = (isCut && sec2 > 0) ? cutSlopeQ (units, 0) * (q / 0.7071f)
                                               : q;

        Coeffs::Ptr c;

        switch ((Fusion::BandType) type)
        {
            case Fusion::BandType::LowShelf:  c = Coeffs::makeLowShelf   (sr, f, q, gl); break;
            case Fusion::BandType::HighShelf: c = Coeffs::makeHighShelf  (sr, f, q, gl); break;
            case Fusion::BandType::Notch:     c = Coeffs::makeNotch      (sr, f, q);      break;
            case Fusion::BandType::BandPass:  c = makeUnitBandPass       (sr, f, q);      break;
            case Fusion::BandType::LowCut:    c = Coeffs::makeHighPass   (sr, fc, qMain); break;
            case Fusion::BandType::HighCut:   c = Coeffs::makeLowPass    (sr, fc, qMain); break;
            case Fusion::BandType::TiltShelf: c = makeTiltShelf          (sr, f, gv);     break;
            case Fusion::BandType::AllPass:   c = Coeffs::makeAllPass    (sr, f, q);      break;
            case Fusion::BandType::Bell:
            default:                          c = Coeffs::makePeakFilter (sr, f, q, gl); break;
        }

        if (c == nullptr)
        {
            for (int i = 0; i < curvePoints; ++i)
                snap.bandCurves[(size_t) b][(size_t) i] = 0.0f;
            continue;
        }

        c->getMagnitudeForFrequencyArray (freqs.data(), tmp.data(),
                                          (size_t) curvePoints, sr);

        /* 低切 / 高切段：把斜率各节一起算进频响（与音频链 runBandSlope 同构），
           否则滚轮改了斜率、画出来的曲线却不变，所见非所听。
           附加二阶节用 Butterworth 梯形 Q（cutSlopeQ(units, s)）；奇数档再乘
           一个一阶节（承担奇数阶的实极点）。所有节同一截止 fc。
           （sec2 / has1 / qMain 已在上面算好。） */
        if (sec2 == 0 && has1)
        {
            /* 6 dB/oct：只有一阶节，主节是恒等旁通 → 直接算一阶的幅度 */
            auto onePole = Fusion::OnePoleHP();
            onePole.setCutoff ((double) fc, sr);

            for (int i = 0; i < curvePoints; ++i)
                tmp[(size_t) i] = onePole.getMagnitudeForFrequency (freqs[(size_t) i], sr);
        }
        else
        {
            for (int s = 1; s < sec2; ++s)
            {
                auto cs = (type == (int) Fusion::BandType::LowCut)
                              ? Coeffs::makeHighPass (sr, fc, cutSlopeQ (units, s))
                              : Coeffs::makeLowPass  (sr, fc, cutSlopeQ (units, s));
                cs->getMagnitudeForFrequencyArray (freqs.data(), tmp2.data(),
                                                    (size_t) curvePoints, sr);
                for (int i = 0; i < curvePoints; ++i)
                    tmp[(size_t) i] *= tmp2[(size_t) i];
            }

            if (has1)
            {
                Fusion::OnePoleHP onePole;
                onePole.setCutoff ((double) fc, sr);

                for (int i = 0; i < curvePoints; ++i)
                    tmp[(size_t) i] *= onePole.getMagnitudeForFrequency (freqs[(size_t) i], sr);
            }
        }

        for (int i = 0; i < curvePoints; ++i)
        {
            const auto v = tmp[(size_t) i];
            snap.bandCurves[(size_t) b][(size_t) i] =
                (float) juce::Decibels::gainToDecibels (v);

            if (! on)
                continue;

            if (mode == Fusion::Mode::Manley)
                parAcc[(size_t) i] += v - 1.0f;          // 并联：贡献线性累加
            else
                mag[(size_t) i] *= v;                     // 串联：幅度相乘
        }
    }

    /* Manley：并联各段合成进总曲线 */
    if (mode == Fusion::Mode::Manley)
        for (int i = 0; i < curvePoints; ++i)
            mag[(size_t) i] *= (float) (1.0 + parAcc[(size_t) i]);

    /* --- 静态倾斜 --- */
    const Fusion::TiltDef defs[3] { m.tiltLow, m.tiltMid, m.tiltHigh };

    for (int t = 0; t < 3; ++t)
    {
        const auto& d   = defs[t];
        const auto  gl  = juce::Decibels::decibelsToGain (d.gainDb);

        Coeffs::Ptr c;
        if      (t == 0) c = Coeffs::makeLowShelf   (sr, d.freq, d.q, gl);
        else if (t == 2) c = Coeffs::makeHighShelf  (sr, d.freq, d.q, gl);
        else             c = Coeffs::makePeakFilter (sr, d.freq, d.q, gl);

        if (c == nullptr)
            continue;

        c->getMagnitudeForFrequencyArray (freqs.data(), tmp.data(),
                                          (size_t) curvePoints, sr);
        for (int i = 0; i < curvePoints; ++i)
            mag[(size_t) i] *= tmp[(size_t) i];
    }

    /* --- Pultec 三段（温润模式）：4 组 shelf 幅频叠加进总曲线 --- */
    if (mode == Fusion::Mode::Pultec)
    {
        for (auto& c : plCoeffs)
        {
            if (c == nullptr)
                continue;

            c->getMagnitudeForFrequencyArray (freqs.data(), tmp.data(),
                                              (size_t) curvePoints, sr);
            for (int i = 0; i < curvePoints; ++i)
                mag[(size_t) i] *= tmp[(size_t) i];
        }
    }

    /* --- 主链曲线（无 AIR） --- */
    for (int i = 0; i < curvePoints; ++i)
    {
        const auto db = (float) juce::Decibels::gainToDecibels (mag[(size_t) i]);
        snap.mainOnly[(size_t) i] = db;
        snap.total   [(size_t) i] = db;
    }

    /* --- 自动增益补偿的目标值 ---
       只看主链（低切 + 6 段 + 静态倾斜），不含 AIR：
       AIR 是平行的音量混合，把它一起补偿掉会失去意义。
       权重按粉噪能量（每倍频程等能量，对数频点天然等权），
       积分区间取 40 Hz–16 kHz —— 两端几乎听不出响度差别，算进去只会让补偿跑偏。 */
    autoGainTargetDb.store (computeAutoGainComp (mag));

    /* seqlock：发布（偶数版本号），读者可以安全拷贝了 */
    snap.airLinear = 0.0f;
    curveIndex.store (back, std::memory_order_release);
    curveSeq.store (seq0 + 2u, std::memory_order_release);
    snapVersion.fetch_add (1, std::memory_order_release);
}

/* 音频线程入口：try-lock，拿不到锁就放弃（返回 false，调用方下个块重试），
   绝不在音频线程上等待。 */
bool FusionEQAudioProcessor::tryUpdateResponseSnapshot()
{
    if (curveWriteBusy.exchange (true, std::memory_order_acquire))
        return false;

    updateResponseSnapshot();
    lastSnapshotMs = juce::Time::getMillisecondCounter();
    curveWriteBusy.store (false, std::memory_order_release);
    return true;
}

/* UI 线程入口：短暂等待音频线程写完后刷新。
   等不到（极端情况）就退而求其次：置 pending 让音频线程下一块刷新。 */
void FusionEQAudioProcessor::refreshCurveFromUI()
{
    int guard = 0;

    while (curveWriteBusy.exchange (true, std::memory_order_acquire))
    {
        if (++guard > 200)                       // ~200 ms 上限，实际几微秒就会拿到
        {
            responsePending.store (true);
            return;
        }

        juce::Thread::sleep (1);
    }

    updateResponseSnapshot();
    curveWriteBusy.store (false, std::memory_order_release);
}

/* 返回使"过 EQ 后的整体响度"回到 0 dB 所需的增益（dB）。
   静态计算：只跟曲线有关，与音乐电平无关 —— 所以不会像 AGC 那样抽吸。 */
float FusionEQAudioProcessor::computeAutoGainComp (const std::array<double, curvePoints>& mag) const noexcept
{
    double sumW = 0.0, sumP = 0.0;

    for (int i = 0; i < curvePoints; ++i)
    {
        const auto f = (double) curveFreqs[(size_t) i];

        if (f < 40.0 || f > 16000.0)
            continue;

        const auto p = mag[(size_t) i];
        sumW += 1.0;
        sumP += p * p;          // 功率平均
    }

    if (sumW < 1.0 || sumP <= 1.0e-12)
        return 0.0f;

    const auto avg = sumP / sumW;
    const auto db  = -10.0 * std::log10 (juce::jmax (1.0e-12, avg));

    return (float) juce::jlimit (-18.0, 18.0, db);
}

/* ============================ 模式 ============================ */
Fusion::Mode FusionEQAudioProcessor::getMode() const noexcept
{
    const auto* p = apvts.getRawParameterValue (IDs::mode);
    return (Fusion::Mode) juce::jlimit (0, Fusion::numModes - 1, (int) std::round (p->load()));
}

void FusionEQAudioProcessor::loadModeDefaults()
{
    const auto& spec = Fusion::getSpec (getMode());

    for (int i = 0; i < Fusion::numBands; ++i)
    {
        const auto& d = spec.bands[(size_t) i];

        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (IDs::bType (i))))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) (int) d.type));

        if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (IDs::bFreq (i))))
            p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 (d.freq));

        if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (IDs::bGain (i))))
            p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 (d.gainDb));

        if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (IDs::bQ (i))))
            p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 (d.q));

        if (auto* p = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (IDs::bOn (i))))
            p->setValueNotifyingHost (0.0f);      // 默认全部 OFF
    }
}

/* ============================ 段状态查询 ============================ */
bool FusionEQAudioProcessor::getBandEnabled (int i) const noexcept
{
    if (! juce::isPositiveAndBelow (i, Fusion::numBands))
        return false;

    return bands[(size_t) i].on;
}

bool FusionEQAudioProcessor::getBandDynOn (int i) const noexcept
{
    return juce::isPositiveAndBelow (i, Fusion::numBands)
        && apvts.getRawParameterValue (IDs::bDynOn (i))->load() > 0.5f;
}

int FusionEQAudioProcessor::getBandChan (int i) const noexcept
{
    return juce::isPositiveAndBelow (i, Fusion::numBands) && getMode() == Fusion::Mode::Clean
        ? (int) apvts.getRawParameterValue (IDs::bChan (i))->load() : 0;
}

float FusionEQAudioProcessor::getBandFreq (int i) const noexcept
{
    if (! juce::isPositiveAndBelow (i, Fusion::numBands))
        return 1000.0f;

    return bands[(size_t) i].freq.getTargetValue();
}

float FusionEQAudioProcessor::getBandGain (int i) const noexcept
{
    if (! juce::isPositiveAndBelow (i, Fusion::numBands))
        return 0.0f;

    return bands[(size_t) i].gain.getTargetValue();
}

float FusionEQAudioProcessor::getBandQ (int i) const noexcept
{
    if (! juce::isPositiveAndBelow (i, Fusion::numBands))
        return 1.0f;

    return bands[(size_t) i].q.getTargetValue();
}

int FusionEQAudioProcessor::getBandType (int i) const noexcept
{
    if (! juce::isPositiveAndBelow (i, Fusion::numBands))
        return 1;

    return bands[(size_t) i].type;
}

float FusionEQAudioProcessor::getBandSlope (int i) const noexcept
{
    if (! juce::isPositiveAndBelow (i, Fusion::numBands))
        return 12.0f;

    return bands[(size_t) i].slopeDb;
}

/* ============================ 编辑器 ============================ */
juce::AudioProcessorEditor* FusionEQAudioProcessor::createEditor()
{
    return new FusionEQAudioProcessorEditor (*this);
}

/* ============================ 状态存取 ============================ */
void FusionEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void FusionEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

/* ============================ 工厂函数 ============================ */
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FusionEQAudioProcessor();
}
