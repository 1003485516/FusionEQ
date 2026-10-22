#!/usr/bin/env bash
# ===========================================================================
#  Fusion EQ — macOS 构建脚本（在 Mac 上运行）
#  产物: build_mac/FusionEQ_artefacts/Release/{VST3,AU,Standalone}
#  前置:
#    1. Xcode 命令行工具 : xcode-select --install
#    2. CMake             : brew install cmake   (需要 3.22+)
#    3. JUCE 7            : 自行下载，用 JUCE_DIR 指向它
#       export JUCE_DIR=/Users/you/Downloads/JUCE
#       或把 JUCE 目录直接放到本项目根目录下的 JUCE/
# ===========================================================================
set -euo pipefail

PROJ="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJ"

# ---- 定位 JUCE ----
if [ -z "${JUCE_DIR:-}" ]; then
    if [ -d "$PROJ/JUCE/CMakeLists.txt" ]; then
        JUCE_DIR="$PROJ/JUCE"
    else
        echo "[X] 找不到 JUCE。请用 JUCE_DIR 环境变量指向它，或把 JUCE 放到本项目根目录/JUCE"
        exit 1
    fi
fi
echo "[*] JUCE: $JUCE_DIR"

# ---- 定位 AAX SDK（可选：没有则不产出 Pro Tools 版）----
# 支持三种来源：环境变量 AAX_SDK_PATH / 项目根 aax-sdk* / ~/sdk/aax-sdk*
if [ -z "${AAX_SDK_PATH:-}" ]; then
    for c in "$PROJ"/aax-sdk* "$HOME"/sdk/aax-sdk*; do
        # SDK 可能有一层或多层嵌套目录，以 Interfaces/AAX.h 为准
        H="$(find "$c" -maxdepth 3 -path '*Interfaces/AAX.h' 2>/dev/null | head -1)"
        if [ -n "$H" ]; then
            AAX_SDK_PATH="$(dirname "$(dirname "$H")")"
            break
        fi
    done
fi
if [ -n "${AAX_SDK_PATH:-}" ] && [ -f "${AAX_SDK_PATH}/Interfaces/AAX.h" ]; then
    echo "[*] AAX SDK: $AAX_SDK_PATH (将产出 Pro Tools 版)"
    AAX_ARG="-DAAX_SDK_PATH=$AAX_SDK_PATH"
else
    echo "[-] 未找到 AAX SDK（可设 AAX_SDK_PATH），跳过 Pro Tools 版"
    AAX_ARG=""
fi

# ---- 选择生成器 ----
if command -v ninja >/dev/null 2>&1; then
    GEN="-G Ninja"
else
    GEN=""   # 回退到 Unix Makefiles
fi

BUILD="$PROJ/build_mac"
# 通用二进制：Intel + Apple Silicon
cmake -B "$BUILD" $GEN \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" \
    -DJUCE_DIR="$JUCE_DIR" $AAX_ARG

cmake --build "$BUILD" --config Release

echo
echo "=== 构建完成 ==="
ls -d "$BUILD/FusionEQ_artefacts/Release/"*/ 2>/dev/null || true
echo
echo "下一步：运行 ./installer/make_dmg.sh 生成 .dmg 安装盘"
