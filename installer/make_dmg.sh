#!/usr/bin/env bash
# ===========================================================================
#  Fusion EQ — 生成 macOS .dmg 安装盘（在 Mac 上运行，需先跑 build_mac.sh）
#  产物: FusionEQ-1.0.0-macOS.dmg（拖拽式：把插件拖进对应文件夹即可）
#  依赖: hdiutil（macOS 自带，无需额外安装）
# ===========================================================================
set -euo pipefail

PROJ="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$PROJ/build_mac"
ART="$BUILD/FusionEQ_artefacts/Release"
APP_NAME="Fusion EQ"

# 版本号优先用环境变量（CI 传入），否则从 CMakeLists.txt 的 project(... VERSION x.y.z) 解析，再回退 1.0.0
if [ -n "${VERSION:-}" ]; then
    :
elif CMVER=$(grep -Eo 'project\([^)]*VERSION[[:space:]]+[0-9]+\.[0-9]+\.[0-9]+' "$PROJ/CMakeLists.txt" 2>/dev/null); then
    VERSION=$(echo "$CMVER" | grep -Eo '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
fi
VERSION="${VERSION:-1.0.0}"

DMG="$PROJ/FusionEQ-${VERSION}-macOS.dmg"
STAGING="$BUILD/dmg_staging"

# ---- 检查产物 ----
if [ ! -d "$ART/VST3/$APP_NAME.vst3" ]; then
    echo "[X] 未找到 VST3，请先运行 ./installer/build_mac.sh"
    exit 1
fi

# ---- 准备暂存区 ----
rm -rf "$STAGING" "$DMG"
mkdir -p "$STAGING"

cp -R "$ART/VST3/$APP_NAME.vst3" "$STAGING/"
[ -d "$ART/AU/$APP_NAME.component" ]        && cp -R "$ART/AU/$APP_NAME.component"        "$STAGING/"
[ -d "$ART/Standalone/$APP_NAME.app" ]      && cp -R "$ART/Standalone/$APP_NAME.app"      "$STAGING/"

# 指向系统目录的替身，方便用户拖拽安装
ln -s /Library/Audio/Plug-Ins/VST3          "$STAGING/VST3 插件文件夹"
ln -s /Library/Audio/Plug-Ins/Components    "$STAGING/AU 插件文件夹"
ln -s /Applications                          "$STAGING/Applications"

# ---- 生成 dmg ----
hdiutil create -volname "Fusion EQ $VERSION" \
    -srcfolder "$STAGING" -ov -format UDZO "$DMG"

# 清理暂存
rm -rf "$STAGING"

echo
echo "=== 已生成: $DMG ==="
echo "安装方法：双击挂载 dmg，把『Fusion EQ.vst3 / .component』拖到对应『插件文件夹』替身。"
echo "注：未签名的插件首次使用需在 系统设置→隐私与安全性 中点『仍要打开』。"
