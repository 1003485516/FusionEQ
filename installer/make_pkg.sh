#!/usr/bin/env bash
# ===========================================================================
#  Fusion EQ — 生成 macOS .pkg 安装包（在 Mac 上运行，需先跑构建）
#  产物: FusionEQ-1.0.0-macOS.pkg
#        AU   -> /Library/Audio/Plug-Ins/Components
#        AAX  -> /Library/Application Support/Avid/Audio/Plug-Ins  (Pro Tools)
#        VST3 -> /Library/Audio/Plug-Ins/VST3
#        App  -> /Applications
#  依赖: pkgbuild / productbuild（macOS 自带，Xcode CLT）
#  可选签名: 设置 MACOS_INSTALLER_IDENTITY + KEYCHAIN_PATH 时自动 productsign
#
#  注意：macOS 自带 bash 为 3.2，不支持关联数组(declare -A)，
#        故全程使用 case 语句 + 普通数组，保证兼容。
# ===========================================================================
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

PKG="$PROJ/FusionEQ-${VERSION}-macOS.pkg"

# ---- 各格式的 扩展名 / 安装位置 / 组件包标识（bash 3.2 兼容：用 case）----
ext_of() {
    case "$1" in
        AU)         echo "component" ;;
        AAX)        echo "aaxplugin" ;;
        VST3)       echo "vst3" ;;
        Standalone) echo "app" ;;
        *)          echo "" ;;
    esac
}
loc_of() {
    case "$1" in
        AU)         echo "/Library/Audio/Plug-Ins/Components" ;;
        AAX)        echo "/Library/Application Support/Avid/Audio/Plug-Ins" ;;
        VST3)       echo "/Library/Audio/Plug-Ins/VST3" ;;
        Standalone) echo "/Applications" ;;
        *)          echo "" ;;
    esac
}
id_of() {
    case "$1" in
        AU)         echo "com.ohaudio.fusioneq.au" ;;
        AAX)        echo "com.ohaudio.fusioneq.aax" ;;
        VST3)       echo "com.ohaudio.fusioneq.vst3" ;;
        Standalone) echo "com.ohaudio.fusioneq.standalone" ;;
        *)          echo "" ;;
    esac
}

# 普通索引数组（bash 3.2 完全支持）
FMTS=(AU AAX VST3 Standalone)

# ---- 定位构建产物目录 ----
BUILD=""
for CAND in "$PROJ/build_mac" "$PROJ/build"; do
    if [ -d "$CAND/FusionEQ_artefacts/Release" ]; then BUILD="$CAND"; break; fi
done
if [ -z "$BUILD" ]; then
    echo "[X] 未找到构建产物目录（期望 build_mac/FusionEQ_artefacts/Release）。请先构建。"
    exit 1
fi
ART="$BUILD/FusionEQ_artefacts/Release"
echo "[*] 产物目录: $ART"

PKGDIR="$BUILD/pkg_components"
rm -rf "$PKGDIR"
mkdir -p "$PKGDIR"

PKGLIST=()
MISSING=()
for F in "${FMTS[@]}"; do
    EXT="$(ext_of "$F")"
    LOC="$(loc_of "$F")"
    ID="$(id_of "$F")"
    SRC="$ART/$F/$APP_NAME.$EXT"
    if [ ! -e "$SRC" ]; then
        MISSING+=("$F")
        echo "[!] 未找到 $F 产物: $SRC"
        continue
    fi
    echo "[*] 处理 $F: $SRC"

    # 暂存并修正所有权为 root:wheel（系统插件目录要求）
    STAGE="$PKGDIR/stage_$F"
    rm -rf "$STAGE"
    mkdir -p "$STAGE"
    cp -R "$SRC" "$STAGE/"
    sudo chown -R root:wheel "$STAGE/$APP_NAME.$EXT" 2>/dev/null || \
        echo "    [!] sudo chown 失败（非致命），所有权将沿用构建用户"

    OUT="$PKGDIR/FusionEQ-${F}.pkg"
    if pkgbuild --component "$STAGE/$APP_NAME.$EXT" \
                --install-location "$LOC" \
                --ownership preserve \
                --identifier "$ID" \
                --version "$VERSION" \
                "$OUT"; then
        PKGLIST+=("$OUT")
        echo "    [+] 组件包 -> $LOC"
    else
        echo "[X] 组件包构建失败: $F"
        exit 1
    fi
done

if [ ${#PKGLIST[@]} -eq 0 ]; then
    echo "[X] 没有任何插件产物，无法打包 PKG。"
    exit 1
fi

# 用户明确要 AU + AAX，缺失则醒目提示
if [ ${#MISSING[@]} -gt 0 ]; then
    for R in AU AAX; do
        FOUND=0
        for M in "${MISSING[@]}"; do
            if [ "$M" = "$R" ]; then FOUND=1; break; fi
        done
        if [ "$FOUND" -eq 1 ]; then
            echo
            echo "[!!] 警告：请求的 $R 格式缺失，PKG 中将不含 $R。"
            if [ "$R" = "AAX" ]; then
                echo "      AAX 需要配置 AAX SDK 私有仓库 Secret（AAX_SDK_REPO + AAX_SDK_PAT）。"
            fi
            echo
        fi
    done
fi

# ---- 合并为单一产品包 ----
ARGS=()
for p in "${PKGLIST[@]}"; do ARGS+=(--package "$p"); done

rm -f "$PKG"
if ! productbuild "${ARGS[@]}" "$PKG"; then
    echo "[X] productbuild 合并失败"
    exit 1
fi

# ---- 可选：对 PKG 进行 productsign（需 Developer ID Installer 证书）----
if [ -n "${MACOS_INSTALLER_IDENTITY:-}" ] && [ -n "${KEYCHAIN_PATH:-}" ]; then
    echo "[*] 使用身份 '$MACOS_INSTALLER_IDENTITY' 对 PKG 进行 productsign ..."
    if productsign --sign "$MACOS_INSTALLER_IDENTITY" "$PKG" "$PKG.signed"; then
        mv "$PKG.signed" "$PKG"
        echo "    [+] PKG 已签名"
    else
        echo "    [!] productsign 失败，保留未签名 PKG"
    fi
else
    echo "[*] 未配置 MACOS_INSTALLER_IDENTITY，跳过 PKG 签名（用户首次安装需『仍要打开』）"
fi

# ---- SHA256 校验和 ----
shasum -a 256 "$PKG" > "$PKG.sha256"

rm -rf "$PKGDIR"

echo
echo "=== 已生成: $PKG ==="
echo "安装：双击 PKG 按向导即可（AU 进 Logic/GarageBand，AAX 进 Pro Tools）。"
echo "未签名 PKG 若被拦截：在 系统设置→隐私与安全性 点『仍要打开』，或 sudo installer -pkg \"$PKG\" -target /"
