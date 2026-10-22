#!/usr/bin/env bash
# ===========================================================================
#  Fusion EQ — 生成 macOS .dmg 安装盘（在 Mac 上运行，需先跑 build_mac.sh）
#  产物: FusionEQ-1.0.0-macOS.dmg（拖拽式：把插件拖进对应文件夹即可）
#  依赖: hdiutil（macOS 自带，无需额外安装）
# ===========================================================================
# 注意：只用 set -uo pipefail，不用 set -e —— 避免命令替换（如 find）返回非零时
#       在没有打印任何诊断信息的情况下静默退出，难以排查 CI 失败。
set -uo pipefail

PROJ="$(cd "$(dirname "$0")/.." && pwd)"
APP_NAME="Fusion EQ"

# ---- 版本号：优先环境变量（CI 传入），否则从 CMakeLists 解析，回退 1.0.0 ----
VERSION="${VERSION:-}"
if [ -z "$VERSION" ]; then
    VERSION="$(grep -Eo 'project\([^)]*VERSION[[:space:]]+[0-9]+\.[0-9]+\.[0-9]+' "$PROJ/CMakeLists.txt" 2>/dev/null \
              | grep -Eo '[0-9]+\.[0-9]+\.[0-9]+' | head -1)"
fi
VERSION="${VERSION:-1.0.0}"

DMG="$PROJ/FusionEQ-${VERSION}-macOS.dmg"

# ---- 定位 VST3 产物（兼容 build_mac / build 两种目录名，最后兜底全盘搜索）----
VST3=""
for CAND in "$PROJ/build_mac" "$PROJ/build" "$PROJ"; do
    [ -d "$CAND" ] || continue
    # 用 -print -quit 取首个匹配并干净退出；不以 set -e 依赖其退出码
    VST3="$(find "$CAND" -name "*.vst3" -maxdepth 6 2>/dev/null | head -1)"
    [ -n "$VST3" ] && break
done
# ---- 若未找到：插件格式目标可能未被默认 all 构建，尝试显式补建再找一次 ----
if [ -z "$VST3" ]; then
    echo "[!] 初次未找到 VST3，尝试显式构建插件格式目标（VST3/AU/Standalone）..."
    BUILDDIR=""
    for CAND in "$PROJ/build_mac" "$PROJ/build"; do
        if [ -d "$CAND" ]; then BUILDDIR="$CAND"; break; fi
    done
    if [ -n "$BUILDDIR" ]; then
        echo "=== Ninja 目标列表（插件格式相关，诊断用）==="
        ninja -C "$BUILDDIR" -t targets 2>/dev/null \
            | grep -oE "^[A-Za-z0-9_]+:" | sort -u \
            | grep -iE "vst3|^au|standalone|fusioneq_" | head -30
        echo "=== 执行: cmake --build --target FusionEQ_VST3 FusionEQ_AU FusionEQ_Standalone ==="
        cmake --build "$BUILDDIR" --config Release \
            --target FusionEQ_VST3 FusionEQ_AU FusionEQ_Standalone \
            || echo "[!] 显式构建插件目标失败，请对照上方目标列表"
        # 补建后重新查找
        VST3="$(find "$BUILDDIR" -name "*.vst3" -maxdepth 6 2>/dev/null | head -1)"
    fi
fi

if [ -z "$VST3" ]; then
    echo "[X] 仍未找到 VST3 产物，请先运行构建（./installer/build_mac.sh 或 CI）。"
    echo "    已搜索目录: $PROJ/build_mac  $PROJ/build  $PROJ(递归)"
    echo "----- build_mac/FusionEQ_artefacts/Release 内容 -----"
    ls -laR "$PROJ/build_mac/FusionEQ_artefacts/Release" 2>/dev/null | head -40
    exit 1
fi

# 由找到的 vst3 反推 Release 目录、构建目录
ART="$(dirname "$(dirname "$VST3")")"        # .../Release
BUILD="$(dirname "$(dirname "$ART")")"        # build_mac 或 build
echo "[*] VST3: $VST3"
echo "[*] 产物目录 ART: $ART"

# 校验 VST3 包确实在预期位置（容错提示）
if [ ! -d "$ART/VST3/$APP_NAME.vst3" ]; then
    echo "[X] 已定位 VST3=$VST3，但预期路径 $ART/VST3/$APP_NAME.vst3 不存在。"
    echo "    实际 $ART 内容:"
    ls -la "$ART" 2>/dev/null || true
    exit 1
fi

STAGING="$BUILD/dmg_staging"

# ---- 准备暂存区 ----
rm -rf "$STAGING" "$DMG"
mkdir -p "$STAGING"

cp -R "$ART/VST3/$APP_NAME.vst3" "$STAGING/"
[ -d "$ART/AU/$APP_NAME.component" ]    && cp -R "$ART/AU/$APP_NAME.component"    "$STAGING/"
[ -d "$ART/Standalone/$APP_NAME.app" ]  && cp -R "$ART/Standalone/$APP_NAME.app"  "$STAGING/"
[ -d "$ART/AAX/$APP_NAME.aaxplugin" ]   && cp -R "$ART/AAX/$APP_NAME.aaxplugin"   "$STAGING/"

# 指向系统目录的替身，方便用户拖拽安装
ln -s /Library/Audio/Plug-Ins/VST3       "$STAGING/VST3 插件文件夹"
ln -s /Library/Audio/Plug-Ins/Components "$STAGING/AU 插件文件夹"
ln -s /Library/Application\ Support/Avid/Audio/Plug-Ins \
                                         "$STAGING/AAX 插件文件夹 (Pro Tools)"
ln -s /Applications                       "$STAGING/Applications"

# ---- 生成 dmg ----
hdiutil create -volname "Fusion EQ $VERSION" \
    -srcfolder "$STAGING" -ov -format UDZO "$DMG"

# 清理暂存
rm -rf "$STAGING"

echo
echo "=== 已生成: $DMG ==="
echo "安装方法：双击挂载 dmg，把『Fusion EQ.vst3 / .component / .aaxplugin』拖到对应『插件文件夹』替身（AAX 需管理员权限）。"
echo "注：未签名的插件首次使用需在 系统设置→隐私与安全性 中点『仍要打开』。"
