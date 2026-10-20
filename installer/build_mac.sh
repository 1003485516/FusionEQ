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
    -DJUCE_DIR="$JUCE_DIR"

cmake --build "$BUILD" --config Release

echo
echo "=== 构建完成 ==="
ls -d "$BUILD/FusionEQ_artefacts/Release/"*/ 2>/dev/null || true
echo
echo "下一步：运行 ./installer/make_dmg.sh 生成 .dmg 安装盘"
