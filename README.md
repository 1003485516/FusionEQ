# Fusion EQ

6 段参数均衡器，六种可选染色模式，VST3 插件（Windows x64）。

---

## 一、直接用（最快）

编译好的插件在：

```
<项目根目录>/Fusion EQ.vst3/          ← 完整 bundle
```

把它整个拷到：

```
C:\Program Files\Common Files\VST3\
```

然后打开 Cubase / Nuendo / Studio One → 插件管理器里重新扫描即可出现 **Fusion EQ**。

> 本次已在构建机上直接安装到该目录。

---

## 二、六种模式

顶部 MODE 区域随时点击切换。差异同时在**四个维度**生效：

| 模式 | Q 行为 | 染色级 | 低切手感 | 静态倾斜 |
|---|---|---|---|---|
| **CLEAN** | 恒定 ×1.00 | 完全线性，零谐波 | 12 dB/oct 巴特沃斯，中性 | 平坦 |
| **MANLEY** | 恒定 ×0.75 | 电子管推挽，偶次谐波，极软拐点 | 12 dB/oct 缓肩，松软 | +低频 −中频 +高频 |
| **PULTEC** | 恒定 ×0.55（极宽） | 末级管轻微圆润化 | 12 dB/oct Q=.50，保住底部重量 | 全频段微抬 |
| **SSL** | 恒定 ×1.35（锐利） | VCA 近似线性，末端快收 | **18 dB/oct**，干净利落 | 中高频微抬 |
| **NEVE** | 恒定 ×0.70 | 变压器厚度，二三次谐波 | **18 dB/oct 带谐振**，收低频更"顶" | 全频段微抬 |
| **API** | **比例 Q**（增益越大越窄） | 离散运放，硬朗冲击 | 12 dB/oct 强谐振，更"打" | 中高频微抬 |

比例 Q 公式（仅 API）：

```
Q_effective = Q_user × (0.36 + 0.065 × |gain_dB|)
```

增益大 → Q 高 → 带宽窄；小增益时极宽。这是 550 系列做音色塑形特别快的原因。

---

## 三、功能

- **LOW CUT**：只有一个开关，**频点不上明面**（内部固定 80 Hz，斜率与谐振随模式变化，见上表）。
  条带上的小响应图随开关变色，下方只标注当前模式的斜率特性。
- **6 段主均衡**：每段 FREQ（20 Hz–20 kHz，对数）/ GAIN（±18 dB）/ Q（0.1–12）。
  每段可点类型键在 **BELL → LOW SHEL → HIGH SHEL** 间循环；第 1 段默认低架，第 6 段默认高架。
- **AIR**：平行 EQ。从输入另拉一条支路，串 2–5 级 11 kHz 高通（级数随模式），
  **只把 11 kHz 以上平行叠加回输出**。只提升不衰减。
  旋钮行程 **八点钟 → 六点钟（300° 顺时针）**，满程 +18 dB，增益用平方律映射。
- **频谱屏**：对数频率轴实时频谱（FFT 4096 / 768 个对数频点，低频端用功率插值消除阶梯；
  滑动窗口 75% 重叠，每前进 512 采样重算一次 ≈ 94 帧/秒，界面 60 fps 刷新平滑流畅）
  + 由 `Coefficients::getMagnitudeForFrequency()` 连乘得到的真实频响曲线，
  以及**每一段单独**的曲线。屏上直接画 6 个可拖动的控制点。
  绘制分三层缓存：背景网格与曲线只在参数变化时重画，每帧只画频谱与控制点。
- **底栏**：AUTO GAIN（自动增益补偿）、OUTPUT、L/R 电平表、峰值保持、载入模式起点。

### AUTO GAIN（自动增益补偿）的工作方式

提升或衰减任何频点都会改变整体音量，音量一变耳朵对"染色"的判断就被干扰。
AUTO GAIN 开启后按 **EQ 曲线本身**计算补偿量：

```
补偿 dB = −10·log10( Σ 10^(L(f)/10) / N )      （f 取 40 Hz–16 kHz，对数等权）
```

- 它是**静态**的：只跟曲线有关，与音乐电平无关 —— 推子起伏、鼓的动态都不会被它"拉平"，
  没有普通 AGC 的抽吸感；
- 拧旋钮时补偿量以 40 ms 平滑跟随，不跳变；
- 只补偿主链（低切 + 6 段 + 模式倾斜），**不含 AIR** —— AIR 本身就是提升型旋钮，
  把它也补掉就失去意义了；
- 限幅 ±18 dB。

实测：+6 dB bell @1 kHz → 补偿 −1.6 dB；+6 dB 低架 @100 Hz → −4.4 dB；平坦曲线 → 0.0 dB。

### 频谱屏交互（Pro-Q 式）

| 操作 | 结果 |
|---|---|
| **空白处按下** | 就在按下的位置**生成**一个控制点，不松手即可继续拖动塑形 |
| 拖动控制点 | 同时改频点（横轴）与增益 / 衰减（纵轴），拖动时有十字参考线 |
| 滚轮（悬停在点上） | 改 Q |
| Ctrl + 滚轮 | 微调增益 |
| 双击控制点 | 该段开 / 关 |
| 鼠标停在机架某段上 | 频谱里实时高亮该段的曲线与频点（拧旋钮立刻能看到变化） |

空白处按下时按下面的优先级挑一个段来承载新控制点：
已关闭的段 → 增益≈0 dB 的段（听感上没在用，挪走不破坏设置）→ 都不存在时仅选中横向最近的一段。

旋钮交互：竖直拖动、滚轮、方向键，双击复位。

---

## 四、从源码编译

### 4.1 需要准备

| 项 | 本次使用的版本 | 说明 |
|---|---|---|
| **CMake** | 3.31.6 | 免安装 zip 即可 |
| **LLVM-MinGW** | 20261006 (clang 23.1.3, UCRT) | GCC 版 MinGW 亦可 |
| **JUCE** | **7.0.12** | ⚠️ 见下方注意事项 |

放置位置（脚本按此查找，可自行改脚本）：

```
C:\Users\Administrator\tools\cmake-3.31.6-windows-x86_64\
C:\Users\Administrator\tools\llvm-mingw-20261006-ucrt-x86_64\
C:\Users\Administrator\sdk\JUCE-7.0.12\
```

### 4.2 ⚠️ 为什么锁定 JUCE 7.0.12

不要换成 JUCE 8 / 9，原因有两个：

1. **JUCE 8 起移除了捆绑的 VST3 SDK**，需另行下载 Steinberg SDK 并指定路径。
2. **JUCE 8 起废弃了 `juce::Font(float, int)` 构造函数**，本工程界面代码大量使用该写法，
   升级需全局改写为 `juce::Font (juce::FontOptions()...)`。

JUCE 7.0.12 自带 `modules/juce_audio_processors/format_types/VST3_SDK`，开箱即用。

### 4.3 需要打的补丁

JUCE 7.0.12（2023 年发布）在 clang ≥ 16 / GCC ≥ 13 下有两处编译错误：

1. `juce_audio_basics/buffers/juce_AudioSampleBuffer.h` 使用了 `std::alignment_of_v`
   却未包含 `<type_traits>`。旧版标准库经由其它头文件间接引入，新版不再引入。
2. `juce_audio_processors/processors/juce_AudioPluginInstance.h` 的模板构造函数把
   退化后的指针 `const short (*)[2]` 传给 `AudioProcessor`，而后者只提供
   `initializer_list` 重载，标准 C++ 下无法匹配。

补丁脚本（幂等，可重复执行）：

```powershell
python C:\Users\Administrator\tools\patch_juce.py
```

若重新下载 JUCE，需重新执行该脚本。

### 4.4 编译

```powershell
cd <项目根目录>\FusionEQ
.\build.bat
```

产物：

```
build\FusionEQ_artefacts\Release\VST3\Fusion EQ.vst3\
```

脚本会自动探测 `llvm-mingw` 或 `w64devkit`，并静态链接 C/C++ 运行时，
因此插件不依赖额外的 `libstdc++` / `libgcc` DLL，拷到别的机器也能直接加载。

---

## 五、源码结构

| 文件 | 作用 |
|---|---|
| `Source/Defs.h` | ModeSpec 模式表 —— 六种音色差异的唯一来源；含格式化函数 |
| `Source/ParamIDs.h` | 参数 ID，Processor 与 Editor 共用 |
| `Source/Shaper.h` | 一阶高通 + 4× 过采样饱和染色级 |
| `Source/PluginProcessor.h/.cpp` | 信号链、参数、频响快照、频谱分析 |
| `Source/FusionKnob.h` | 旋钮外观与拖拽交互 |
| `Source/SpectrumDisplay.h/.cpp` | 频谱与频响曲线绘制 |
| `Source/Strips.h` | 低切 / 6 段 / AIR 三类机架条带 |

信号链：

```
in ──> HPF(80Hz) ──> 6× Band ──> 3× Tilt ──> Color(过采样饱和) ──┐
  │                                                                ├─> out
  └──> AIR: N× HP(11kHz) ──> airGain ─────────────────────────────┘
```

AIR 支路与主链完全独立，仅平行叠加回输出。

---

## 六、已知说明

- 界面为浅色专业音频风格，固定 1180×860，不可缩放。
- 六种模式切换时**保留当前段参数**，便于 A/B 对比染色差异；
  需要回到某个模式的标志性起点时点底部「载入模式起点」。
- 过采样固定 4×，延迟约 8 样本（已在宿主侧声明）。

### 三个容易踩的坑（已修，改代码时注意）

1. **中文显示**：两道坎——
   ① JUCE 的 `String(const char*)` 走 `CharPointer_ASCII`，会把 UTF-8 字节逐字节当
   Latin-1 解释 → 源码里所有中文必须写成 `Fusion::U8("\uXXXX…")`；
   ② Windows 默认字体 Segoe UI 没有 CJK 字形，DirectWrite 渲染**不会自动回退**，
   中文会整段消失 → `FusionLook::getTypefaceForFont` 把默认字体统一解析到
   微软雅黑，并装在 `LookAndFeel::setDefaultLookAndFeel` 上
   （注意：字形解析只走**默认** LookAndFeel，挂在组件上的 LNF 无效）。
2. **切换模式不该改变音量**：饱和曲线统一归一化为 `y(0)=0, y'(0)=1`（小信号斜率 1）。
   早期用 `tanh(k·x)/tanh(k)` 做满量程归一化，会把小信号放大 2.5 倍，
   那正是 EQP / NEVE / API 三档"音量暴涨 + 失真"的根因。
   现在六档在**平坦曲线**下的宽带增益完全一致（白噪实测差 0.00 dB），
   各档 `makeup` 就是按实测校准的静态补偿值。
   切模式若仍觉得音量有变，那是该模式自己的倾斜 / 低切斜率造成的（粉噪实测 ±1.8 dB 内），属设计意图。
   校准脚本：`<项目根目录>/verify_loudness.py`。
3. **频谱帧率**：频率分辨率（FFT 4096）与时间分辨率天然矛盾。
   若等攒满 4096 采样才算一次，48 kHz 下只有 ~12 帧/秒，画面一顿一顿。
   解法是滑动窗口 + 75% 重叠（每 512 采样算一次），平滑系数按时间常数
   （上升 12 ms / 回落 90 ms）换算，与帧率无关。
