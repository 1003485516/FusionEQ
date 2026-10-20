cmake_minimum_required(VERSION 3.22)

project(FusionEQ VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# MinGW 构建：静态链接 C/C++ 运行时，让插件不依赖外部 DLL，
# 拷到别的机器也能直接加载。
# ---------------------------------------------------------------------------
if(MINGW)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++")
    set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -static-libgcc")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc -static-libstdc++ -Wl,--exclude-libs,ALL")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
    add_compile_options(-mfpmath=sse -msse2)
endif()

# ---------------------------------------------------------------------------
# 定位 JUCE。三种方式任选其一：
#   cmake -B build -DJUCE_DIR=C:/JUCE
#   或设置环境变量 JUCE_DIR
#   或把 JUCE 放在本目录下的 JUCE/ 里
# ---------------------------------------------------------------------------
if(NOT DEFINED JUCE_DIR)
    if(DEFINED ENV{JUCE_DIR})
        set(JUCE_DIR "$ENV{JUCE_DIR}")
    elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/JUCE/CMakeLists.txt")
        set(JUCE_DIR "${CMAKE_CURRENT_LIST_DIR}/JUCE")
    endif()
endif()

if(NOT DEFINED JUCE_DIR OR NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "找不到 JUCE。请先下载 JUCE (https://juce.com/get-juce/download)，然后用以下任一方式指定：\n"
        "  cmake -B build -DJUCE_DIR=C:/path/to/JUCE\n"
        "  设置环境变量 JUCE_DIR\n"
        "  或把 JUCE 目录放到 ${CMAKE_CURRENT_LIST_DIR}/JUCE")
endif()

message(STATUS "使用 JUCE: ${JUCE_DIR}")
add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)

# ---------------------------------------------------------------------------
# 平台相关的插件格式
#   macOS ：VST3 + AU + Standalone（Logic / GarageBand 只认 AU）
#   Windows：VST3 + Standalone
# ---------------------------------------------------------------------------
if(APPLE)
    # 注意：必须是 CMake「列表」（分号分隔 / 无引号多值），
    # 不能写成 "VST3 AU Standalone" —— 带引号的空格字符串会被当成【单个值】，
    # CMake 只按分号拆分，JUCE 会因匹配不到任何格式而不创建任何插件目标
    # （表现：只产出 SharedCode 静态库，没有 VST3/AU/Standalone，Ninja 里找不到 FusionEQ_VST3）。
    set(FUSION_FORMATS VST3 AU Standalone)
    # 最低部署目标（覆盖绝大多数仍在用的 Mac）
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "macOS deployment target" FORCE)
    endif()
else()
    set(FUSION_FORMATS VST3 Standalone)
endif()

# 调试用：确认最终传给 juce_add_plugin 的格式列表
message(STATUS "插件格式 FORMATS = ${FUSION_FORMATS}")

# ---------------------------------------------------------------------------
# 插件目标
# ---------------------------------------------------------------------------
juce_add_plugin(FusionEQ
    COMPANY_NAME          "Fusion Audio"
    BUNDLE_ID             "com.fusionaudio.fusioneq"
    PLUGIN_MANUFACTURER_CODE  Fusn
    PLUGIN_CODE            FuEQ
    FORMATS                ${FUSION_FORMATS}  # macOS=VST3+AU+Standalone；Windows=VST3+Standalone
    PRODUCT_NAME           "Fusion EQ"
    IS_SYNTH               FALSE
    NEEDS_MIDI_INPUT       FALSE
    NEEDS_MIDI_OUTPUT      FALSE
    IS_MIDI_EFFECT         FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES        EQ
    AU_MAIN_TYPE           kAudioUnitType_Effect
    AU_EXPORT_PREFIX       FusionEQAU
)

# 生成 JuceHeader.h；普通插件必须显式调用，
# 否则源码里的 #include <JuceHeader.h> 会找不到文件。
juce_generate_juce_header(FusionEQ)

# SSL 4000E 四段旋钮贴图（编译期嵌入，运行时零文件依赖）
juce_add_binary_data(FusionEQData SOURCES
    Resources/knob_ssl_black.png
    Resources/knob_ssl_green.png
    Resources/knob_ssl_blue.png
    Resources/knob_ssl_purple.png
    Resources/knob_manley.png
    Resources/knob_pultec.png
    Resources/knob_lever.png
    Resources/knob_atten.png
    Resources/knob_bandwidth.png
    Resources/knob_air.png
    Resources/knob_ssl_red.png
    Resources/knob_neve.png
    Resources/knob_blue.png
    Resources/knob_api_freq.png
    Resources/knob_api_gain.png
    Resources/knob_red.png
    Resources/knob_blue_glass.png
    Resources/knob_api215.png
    Resources/knob_api_air.png
    Resources/btn_hp_off.png
    Resources/btn_hp_on.png
    Resources/btn_lp_off.png
    Resources/btn_lp_on.png
    Resources/btn_low_off.png
    Resources/btn_low_on.png
    Resources/btn_mid_off.png
    Resources/btn_mid_on.png
    Resources/btn_high_off.png
    Resources/btn_high_on.png
    Resources/btn_lf_off.png
    Resources/btn_lf_on.png
    Resources/btn_lmf_off.png
    Resources/btn_lmf_on.png
    Resources/btn_hmf_off.png
    Resources/btn_hmf_on.png
    Resources/btn_hf_off.png
    Resources/btn_hf_on.png
    Resources/btn_air_off.png
    Resources/btn_air_on.png
    Resources/gem_off.png
    Resources/gem_on.png
    Resources/btn_ssl_curve_off.png
    Resources/btn_ssl_curve_on.png
    Resources/panel_eqp.png
)

target_sources(FusionEQ PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/SpectrumDisplay.cpp
)

target_compile_definitions(FusionEQ PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(FusionEQ PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    FusionEQData
)
