/* =============================================================================
   Fusion EQ  ·  PluginProcessor.h
   ========================================================================== */
#pragma once
#include <JuceHeader.h>
#include "Defs.h"
#include "Shaper.h"

class FusionEQAudioProcessor : public juce::AudioProcessor
{
public:
    FusionEQAudioProcessor();
    ~FusionEQAudioProcessor() override;

    /* ---------------- AudioProcessor ---------------- */
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /* ---------------- 参数 ---------------- */
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /* ---------------- 供 UI 读取的快照（双缓冲，无锁） ---------------- */
    static constexpr int curvePoints  = 512;
    static constexpr int spectrumBins = 768;   // 更细的对数频点（约 1.5 px / 点）

    struct CurveSnapshot
    {
        std::array<float, curvePoints>  total    {};
        std::array<float, curvePoints>  mainOnly {};
        /* 每段单独的响应（dB），供频谱上绘制单段曲线 */
        std::array<std::array<float, curvePoints>, Fusion::numBands> bandCurves {};
        float                           airLinear = 0.0f;
    };

    /* 返回当前快照的**拷贝**（seqlock 重试，杜绝界面读到写到一半的撕裂帧） */
    bool readCurve (CurveSnapshot& out) const noexcept;
    bool readSpectrum (std::array<float, spectrumBins>& out) const noexcept;

    /* 快照所用的对数频点表（prepare 后不变） */
    /* 快照版本号：每发布一次新快照 +1，UI 每帧对比即可感知
       "音频线程在背后重算过快照"（如播放中切模式），及时重画曲线 */
    std::atomic<juce::uint64> snapVersion { 0 };
    const std::array<float, curvePoints>& getCurveFreqs() const noexcept { return curveFreqs; }

    /* UI 线程刷新频响快照：updateResponseSnapshot 完全自包含
       （直接从参数现算系数，不读音频线程的成员），
       宿主停止播放时低切开关等也能立刻反映到曲线上。 */
    void refreshCurveFromUI();

    Fusion::Mode getMode() const noexcept;

    /* 把当前模式的标志性起点写入参数（UI 的 LOAD 按钮调用） */
    void loadModeDefaults();

    /* ---------------- 每模式独立参数快照 ----------------
       切换模式时：capture 旧模式 → restore 新模式。
       频段（类型/频点/增益/Q/开关）、低切、SSL 滤波、Magic Air
       都按模式各自记忆，互不带走。 */
    struct ModeState
    {
        float freq[Fusion::numBands] = {};
        float gain[Fusion::numBands] = {};
        float q[Fusion::numBands]    = {};
        bool  on[Fusion::numBands]   = {};
        int   type[Fusion::numBands] = {};

        bool  hpfOn = false;
        bool  lpOn = false;      // 硬朗 215L：LO-PASS 独立开关
        int   hpfSlope = 0;
        int   hpfFreq = 1;    // 低切频点档位 0-3（50/80/160/300 Hz）
        float filtHp = 20.0f, filtLp = 20000.0f;
        bool  airOn = false;
        float airAmt = 0.0f;
    };

    void initModeStates();
    void captureModeState (int m);
    void restoreModeState (int m);

    ModeState modeStates[Fusion::numModes];

    /* 供频谱屏上画各段控制点 / 单段曲线 */
    bool  getBandEnabled (int i) const noexcept;
    float getBandFreq    (int i) const noexcept;
    float getBandGain    (int i) const noexcept;
    float getBandQ       (int i) const noexcept;
    int   getBandType    (int i) const noexcept;

    /* 动态压缩（干净模式）：开关 + 当前 GR（dB，负值；供悬浮窗显示） */
    bool  getBandDynOn (int i) const noexcept;
    float getBandDynGr (int i) const noexcept { return dynGrPub[(size_t) i].load(); }

    /* 每段声道模式（干净模式）：0=Stereo 1=Left 2=Right 3=Mid 4=Side */
    int   getBandChan  (int i) const noexcept;

    /* 自动增益补偿当前值（dB，供 UI 显示） */
    float getAutoGainDb() const noexcept { return autoGainDb.load(); }

    /* 旁路（UI 触发；不占用自动化参数位） */
    void setBypassed (bool b) noexcept { bypassed.store (b); }
    bool isBypassed()  const noexcept  { return bypassed.load(); }

    /* 输出电平（供电平表） */
    float getOutLevel (int ch) const noexcept
    { return ch == 0 ? outL.load() : outR.load(); }

private:
    /* ---------------- 段状态 ---------------- */
    struct BandState
    {
        juce::SmoothedValue<float> freq { 1000.0f };
        juce::SmoothedValue<float> q    { 1.0f };
        juce::SmoothedValue<float> gain { 0.0f };
        int  type = 1;          // 0=LowShelf 1=Bell 2=HighShelf
        bool on   = true;
        bool dirty = true;
        std::array<juce::dsp::IIR::Filter<float>, 2> filters {};   // [ch]

        /* --- 动态压缩（干净模式专属，F6 式）---
           检测支路用同一组系数的独立实例（不动主链状态），
           包络/增益计算机双通道共享（max(|L|,|R|) 立体声联动） */
        std::array<juce::dsp::IIR::Filter<float>, 2> detFilters {};   // [ch] 检测带通
        bool  dynOn  = false;
        float dynThr = -40.0f;  // dB
        float dynRng = 6.0f;    // dB（最大衰减）
        float dynAtt = 8.0f;    // ms
        float dynRel = 150.0f;  // ms
        float env    = 0.0f;    // 峰值包络（线性）
        float grDb   = 0.0f;    // 当前增益衰减（dB，负值）
        int   chan   = 0;       // 声道模式：0=Stereo 1=Left 2=Right 3=Mid 4=Side
    };
    std::array<BandState, Fusion::numBands> bands {};

    /* ---------------- 低切 ---------------- */
    struct HpfState
    {
        /* [stage][ch]：最多 3 级二阶级联 = 36 dB/oct；实际级数由 hpfSlope 决定。
           级联本身永远是纯 Butterworth（模式性格不参与，见 character）。 */
        std::array<std::array<juce::dsp::IIR::Filter<float>, 2>, 3> biquad {};
        std::array<Fusion::OnePoleHP, 2>              onePole {};     // [ch]（保留，现不使用）

        /* 模式肩部性格节 [ch]：固定的峰/谷（厚实/硬朗 = 谐振凸起，
           温暖/温润 = 转折凹陷），与斜率无关 —— 保证 12/24/36 dB/oct
           下性格幅度严格一致。 */
        std::array<juce::dsp::IIR::Filter<float>, 2>  character {};
    };
    HpfState hpf {};
    bool hpfOn = true;
    bool lpOn = true;            // 硬朗 215L：LO-PASS（低切）独立开关
    int  hpfStages = 1;          // 1 = 12 dB/oct；2 = 24；3 = 36
    int  lastHpfSlope = -1;      // 斜率变化 → 重建各级 Q（Butterworth 对齐）
    float hpfFreqHz = 80.0f;     // 低切转折频率（步进 50/80/160/300）
    int  lastHpfFreq = -1;       // 频点档位变化 → 重建系数

    /* ---------------- SSL 滤波（原版 4000 HP/LP） ----------------
       固定 12 dB/oct 二阶节、常开，只调频点；仅 SSL 模式接入信号链。 */
    struct FiltState { std::array<juce::dsp::IIR::Filter<float>, 2> hp {}, lp {}; };
    FiltState filt {};

    /* Manley Massive Passive：LOW PASS 原机 18 dB/oct（12 dB 二阶 + 6 dB 一阶补一级） */
    juce::dsp::IIR::Coefficients<float>::Ptr filtLp1Coeffs;
    std::array<juce::dsp::IIR::Filter<float>, 2> filtLp1 {};
    using CoeffsIIR = juce::dsp::IIR::Coefficients<float>;
    CoeffsIIR::Ptr filtHpCoeffs, filtLpCoeffs;
    float filtHpHz = 20.0f, filtLpHz = 20000.0f;    // 默认端点 = OUT（旁通）
    void rebuildFilt();

    /* ---------------- 静态倾斜 ---------------- */
    struct TiltState { std::array<juce::dsp::IIR::Filter<float>, 2> f {}; };  // [ch]
    std::array<TiltState, 3> tilt {};   // low / mid / high

    /* 频点监听（SOLO LISTEN）：>0 = 正在监听的频段，-1 = 关闭 */
    std::atomic<int> listenBand { -1 };
    std::array<juce::dsp::IIR::Filter<float>, 2> listenFilters {};   // [ch] 单位增益带通
    int   lastListenBand = -1;
    float lastListenF = -1.0f, lastListenQ = -1.0f;

public:
    void setListenBand (int b) noexcept          { listenBand.store (b, std::memory_order_relaxed); }
    int  getListenBand() const noexcept          { return listenBand.load (std::memory_order_relaxed); }

private:

    /* ---------------- 染色 ---------------- */
    std::array<Fusion::ColorStage, 2> color {};   // [ch]

    /* ---------------- 增益 ---------------- */
    juce::SmoothedValue<float> outGain  { 1.0f };
    juce::SmoothedValue<float> airGain  { 0.0f };

    /* 自动增益补偿
       依据「EQ 曲线本身的响度变化」做静态补偿，而不是跟随节目电平的 AGC。
       目标值在 updateResponseSnapshot() 里由幅频响应加权平均得出，
       这里只负责 40 ms 平滑淡入淡出（拧旋钮时不跳变，也不会跟着音乐起伏）。 */
    juce::SmoothedValue<float> autoGain { 1.0f };
    std::atomic<float> autoGainTargetDb { 0.0f };
    std::atomic<float> autoGainDb       { 0.0f };
    bool  autoGainOn   = false;

    /* 过采样倍率：0 = X1（零延迟）；1 = X2；2 = X4；3 = X8；4 = X16（默认，最好音质） */
    int   osPow = 4;

    /* ---------------- AIR 平行路径 ---------------- */
    static constexpr int maxAirStages = 8;
    std::array<std::array<juce::dsp::IIR::Filter<float>, 2>, maxAirStages> airHP {};  // [stage][ch]
    bool  airOn  = true;
    float airAmt = 0.0f;

    /* ---------------- Pultec EQP-1A 三段式（温润模式专属） ----------------
       low:  boost shelf @cps + atten shelf @cps*0.75（两者可同时 → 经典 low-end trick）
       high: boost shelf @kcs（Q 由 SHARP/BROAD 决定）+ atten shelf @kcs*1.15 */
    struct PultecShelf { std::array<juce::dsp::IIR::Filter<float>, 2> ch {}; };
    PultecShelf plLowBoostF, plLowAttenF, plHighBoostF, plHighAttenF;
    float plLowBoostV = 0.0f, plLowAttenV = 0.0f;
    float plHighBoostV = 0.0f;
    int   plLowFreqI = 1, plHighFreqI = 2;                 // 默认 30 Hz / 10 kHz
    float plBandV = 0.5f;                                  // 线性带宽 0=BROAD 1=SHARP
    float plHighAttenAmtV = 0.0f;                         // ATTEN：0-10 衰减量
    int   plHighAttenI = 0;                               // ATTEN SEL：0/1/2 → 5/10/20 KC 转折频率

    double currentSampleRate = 48000.0;
    Fusion::Mode lastMode = Fusion::Mode::Clean;

    /* AIR 支路用的暂存缓冲（避免在处理中改动主链） */
    juce::AudioBuffer<float> airScratch;

    /* Manley Massive Passive 并联拓扑暂存：mpSum = 干 + Σ(各段输出 − 干) */
    juce::AudioBuffer<float> mpSum, mpBand;

    /* 动态压缩（干净模式）：dynDry = 本块输入副本；dynDet = 检测支路输出；
       dynBand = 段输出副本；dynGrCurve = 每段逐样本增益曲线（线性，段独立）；
       laBuf = 每段每声道 look-ahead 环形缓冲 [band][ch][pos]：
       段贡献 delta 先延迟 laSize 采样，增益曲线提前作用 —— 前视压缩 */
    juce::AudioBuffer<float> dynDry, dynDet, dynBand;
    std::array<std::vector<float>, Fusion::numBands> dynGrCurve {};
    std::vector<float> grOneCurve;                 // 非动态的特殊声道段：恒定 1.0 增益曲线
    std::array<std::vector<float>, 2> dynLate {};  // Mid/Side 段：ch0 算好的另一声道贡献
    std::vector<float> laBuf;
    int laPos = 0, laSize = 0;
    std::array<std::atomic<float>, Fusion::numBands> dynGrPub {};   // UI 读数（dB）

    /* ---------------- 系数（供频响曲线计算） ---------------- */
    using Coeffs = juce::dsp::IIR::Coefficients<float>;
    std::array<Coeffs::Ptr, Fusion::numBands> bandCoeffs {};
    std::array<Coeffs::Ptr, 3>                tiltCoeffs {};
    std::array<Coeffs::Ptr, 3>                hpfCoeffs  {};   // [stage]（Butterworth 对齐，各级 Q 不同）
    std::vector<Coeffs::Ptr>                  airCoeffs  {};
    std::array<Coeffs::Ptr, 4>                plCoeffs   {};   // Pultec [lowB, lowA, highB, highA]

    /* ---------------- 频谱分析 ----------------
       有频率分辨率（4096 点）又要时间分辨率，所以用滑动窗口：
       每次只前进 hopSize 个采样就重算一次 FFT（75% 重叠）。
       48 kHz 下 ≈ 94 次/秒，足够 60 fps 的界面平滑刷新。 */
    static constexpr int fftOrder = 12;                 // 4096（更高频率分辨率）
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int hopSize  = 512;                // 每次 FFT 的推进量
    juce::dsp::FFT           forwardFFT { fftOrder };
    juce::dsp::WindowingFunction<float> window {
        (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, fftSize * 2> fftData {};
    juce::AudioBuffer<float>       fftHistory;           // 1 声道 × fftSize 的环形历史
    int histIndex = 0;
    int hopCount  = 0;
    std::array<float, spectrumBins> specWork {};
    std::array<std::array<float, spectrumBins>, 2> spectrumBuf {};
    std::atomic<int> spectrumIndex { 0 };

    /* 预计算的对数频点 → FFT bin 边界（避免每次分析都调 pow） */
    std::array<double, spectrumBins> binPos0 {};
    std::array<double, spectrumBins> binPos1 {};
    bool spectrumMapReady = false;

    std::array<float, curvePoints> curveFreqs {};

    /* 频响计算用的临时缓冲（成员化 = 音频线程不分配内存） */
    std::array<double, curvePoints> respFreqs {}, respTmp {}, respMag {}, respAir {};
    std::array<CurveSnapshot, 2>   curveBuf   {};
    std::atomic<int>               curveIndex { 0 };

    /* --- 快照发布协议 ---
       seqlock：curveSeq/specSeq 为奇数表示"正在写入"，读者拷贝前后各读一次，
       版本号不一致就重试 —— 界面永远不会看到写到一半的撕裂帧（拖动时"扯一下"）。
       curveWriteBusy 串行化两个写者（音频线程 / UI 线程）：
       音频线程 try-lock，拿不到就跳过（下个块再试，绝不阻塞）；UI 线程短暂等待。 */
    mutable std::atomic<juce::uint32> curveSeq { 0 };
    mutable std::atomic<juce::uint32> specSeq  { 0 };
    std::atomic<bool> curveWriteBusy { false };

    /* 参数刚变化 → 下一块重算频响快照 */
    std::atomic<bool> responsePending { true };

    std::atomic<bool>  bypassed { false };
    std::atomic<float> outL { 0.0f };
    std::atomic<float> outR { 0.0f };

    void pushSamplesForAnalysis (const juce::AudioBuffer<float>&);
    void prepareSpectrumMap();
    void runAnalysis();
    void updateResponseSnapshot();          // 调用前必须已持有 curveWriteBusy
    bool tryUpdateResponseSnapshot();       // 音频线程用：拿不到锁返回 false
    float computeAutoGainComp (const std::array<double, curvePoints>& mag) const noexcept;
    void rebuildHpf();
    void rebuildAir();
    void rebuildPultec();
    void updateBandCoefficients (int bandIndex);
    void updateTiltCoefficients();
    float effectiveQ (const BandState&) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FusionEQAudioProcessor)
};
