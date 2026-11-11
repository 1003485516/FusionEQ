#!/usr/bin/env bash
# ===========================================================================
#  Fusion EQ — Mac 本机补齐 AAX 的 PACE 签名，并产出可发布的签名 + 公证 PKG
#
#  前提：
#    1. 已取得 CI 产出的 FusionEQ-PACE-ready.zip
#       （VST3 / AU / Standalone 已 Apple codesign；AAX 未签名）
#    2. 本机已安装 PACE Eden Fusion Tools（wraptool）+ iLok License Manager，
#       并已在 PACE Central 激活 PACE Central Access 许可证
#    3. 钥匙串里有 Developer ID Application / Developer ID Installer 两张证书及私钥
#
#  用法（在 Mac 上，把本脚本与 zip 放同一目录）：
#    export PACE_ACCOUNT=...
#    export PACE_WCGUID=...
#    export PACE_PASSWORD=...            # iLok 账号密码；不设则交互式输入
#    export APPLE_ID=...
#    export APPLE_APP_SPECIFIC_PASSWORD=...
#    export APPLE_TEAM_ID=...
#    bash mac_pace_finish.sh
#
#  完成后把 FusionEQ-1.0.35-macOS.pkg 与 .sha256 发回 Windows 端上线官网。
#  注意：macOS 自带 bash 3.2，本脚本不使用关联数组等 4.x 特性。
# ===========================================================================
set -euo pipefail

VERSION="${VERSION:-1.0.35}"
APP_NAME="Fusion EQ"
ZIP="${PACE_READY_ZIP:-./FusionEQ-PACE-ready.zip}"
OUT_PKG="$(pwd)/FusionEQ-${VERSION}-macOS.pkg"

PACE_ACCOUNT="${PACE_ACCOUNT:-}"
PACE_WCGUID="${PACE_WCGUID:-}"
PACE_SIGNID="${PACE_SIGNID:-Developer ID Application: ling chen (S9JL9DJU7A)}"
INSTALLER_IDENTITY="${INSTALLER_IDENTITY:-Developer ID Installer: ling chen (S9JL9DJU7A)}"
APPLE_ID="${APPLE_ID:-}"
APPLE_APP_SPECIFIC_PASSWORD="${APPLE_APP_SPECIFIC_PASSWORD:-}"
APPLE_TEAM_ID="${APPLE_TEAM_ID:-S9JL9DJU7A}"
# 首次公证若报 hardened runtime / timestamp 相关问题，设为 "--dsigharden" 后重跑
PACE_EXTRA_ARGS="${PACE_EXTRA_ARGS:-}"

# ---- 定位 wraptool ----
WRAPTOOL=""
for C in \
  "/Applications/PACEAntiPiracy/Eden/Fusion/Versions/6/bin/wraptool" \
  "/Applications/PACEAntiPiracy/Eden/Fusion/Current/bin/wraptool" \
  "/Applications/PACEAntiPiracy/Eden/Fusion/Versions/5/bin/wraptool"; do
  if [ -x "$C" ]; then WRAPTOOL="$C"; break; fi
done
if [ -z "$WRAPTOOL" ]; then
  echo "[X] 未找到 wraptool。请先从 PACE Central 安装 Eden Fusion Tools。"
  exit 1
fi
echo "[*] wraptool: $WRAPTOOL"

if [ -z "$PACE_ACCOUNT" ] || [ -z "$PACE_WCGUID" ]; then
  echo "[X] 必须设置 PACE_ACCOUNT 与 PACE_WCGUID（见操作说明）。"
  exit 1
fi
if [ -z "${PACE_PASSWORD:-}" ]; then
  read -rs -p "请输入 iLok 账号密码（输入不回显）: " PACE_PASSWORD
  echo
fi
if [ -z "$APPLE_ID" ] || [ -z "$APPLE_APP_SPECIFIC_PASSWORD" ]; then
  echo "[X] 必须设置 APPLE_ID 与 APPLE_APP_SPECIFIC_PASSWORD（公证用）。"
  exit 1
fi
if [ ! -f "$ZIP" ]; then
  echo "[X] 找不到 $ZIP"
  exit 1
fi

# ---- 1. 解压构建产物 ----
WORK="$(pwd)/pace_work"
rm -rf "$WORK"
mkdir -p "$WORK/FusionEQ_artefacts"
# 校验传输完整性（与 zip 同目录的 .sha256 由 CI 生成）
if [ -f "$ZIP.sha256" ]; then
  echo "[*] 校验下载文件 SHA256 ..."
  EXPECT=$(awk '{print $1}' "$ZIP.sha256")
  ACTUAL=$(shasum -a 256 "$ZIP" | awk '{print $1}')
  if [ "$EXPECT" != "$ACTUAL" ]; then
    echo "[X] SHA256 不一致（期望 $EXPECT，实际 $ACTUAL），请重新下载"
    exit 1
  fi
  echo "    [+] OK $ACTUAL"
fi
echo "[*] 解压 $ZIP ..."
ditto -x -k "$ZIP" "$WORK/FusionEQ_artefacts"
ART="$WORK/FusionEQ_artefacts/Release"
if [ ! -d "$ART" ]; then
  # 兼容 zip 内没有 Release 顶层的情况
  ART="$WORK/FusionEQ_artefacts"
fi
for F in VST3 AU AAX Standalone; do
  if [ ! -d "$ART/$F" ]; then echo "[X] 缺少 $F 产物"; exit 1; fi
done
AAX="$ART/AAX/$APP_NAME.aaxplugin"
echo "[*] AAX: $AAX"

# ---- 2a. 确认其余三个组件的 Apple 签名在跨机器传输后仍有效 ----
echo "[*] 校验 VST3 / AU / Standalone 的 Apple 签名 ..."
for F in VST3 AU Standalone; do
  case "$F" in
    VST3) EXT="vst3" ;;
    AU)   EXT="component" ;;
    Standalone) EXT="app" ;;
  esac
  COMP="$ART/$F/$APP_NAME.$EXT"
  if ! codesign --verify --deep --strict --verbose=1 "$COMP" 2>&1; then
    echo "[X] $F 的 Apple 签名在解压后失效，不能继续公证。"
    echo "    可用本机 Developer ID Application 身份重签："
    echo "    codesign --force --options runtime --timestamp --sign \"$PACE_SIGNID\" \"$COMP\""
    exit 1
  fi
  echo "    [+] $F 签名有效"
done

# ---- 2b. 确认 AAX 是未签名状态（wraptool 要求干净的输入） ----
if codesign -dv "$AAX" >/dev/null 2>&1; then
  echo "[!] AAX 已带 Apple 签名，先移除（wraptool 会自己重新 codesign）..."
  codesign --remove-signature "$AAX"
fi
echo "[*] 备份未签名 AAX 到 $WORK/unsigned-backup/"
mkdir -p "$WORK/unsigned-backup"
cp -R "$AAX" "$WORK/unsigned-backup/"

# ---- 3. PACE / wraptool 签名 ----
# 注意：Mac 上 --signid 是 Apple Developer ID Application 身份名（或 40 位身份 ID），
#       不是 Windows 那张 Authenticode 证书的 SHA-1 指纹。
echo "[*] wraptool sign ..."
# shellcheck disable=SC2086
"$WRAPTOOL" sign --verbose \
  --account "$PACE_ACCOUNT" \
  --wcguid "$PACE_WCGUID" \
  --signid "$PACE_SIGNID" \
  --in "$AAX" \
  --out "$AAX" \
  --autoinstall off \
  --password "$PACE_PASSWORD" \
  $PACE_EXTRA_ARGS

# ---- 4. 双重验证 ----
echo "[*] wraptool verify ..."
"$WRAPTOOL" verify --verbose --in "$AAX"
echo "[*] codesign --verify --deep --strict ..."
codesign --verify --deep --strict --verbose=2 "$AAX"
codesign -dv --verbose=4 "$AAX" 2>&1 | grep -E "Identifier|TeamIdentifier|Timestamp|Signature" || true

# ---- 5. 重新打 PKG（四个组件各一个 pkgbuild，再 productbuild 合并） ----
echo "[*] 构建组件包 ..."
PKGDIR="$WORK/pkg_components"
rm -rf "$PKGDIR"
mkdir -p "$PKGDIR"
PKGLIST=()
for F in AU AAX VST3 Standalone; do
  case "$F" in
    AU)         EXT="component"; LOC="/Library/Audio/Plug-Ins/Components";              ID="com.ohaudio.fusioneq.au" ;;
    AAX)        EXT="aaxplugin"; LOC="/Library/Application Support/Avid/Audio/Plug-Ins"; ID="com.ohaudio.fusioneq.aax" ;;
    VST3)       EXT="vst3";      LOC="/Library/Audio/Plug-Ins/VST3";                     ID="com.ohaudio.fusioneq.vst3" ;;
    Standalone) EXT="app";       LOC="/Applications";                                     ID="com.ohaudio.fusioneq.standalone" ;;
  esac
  COMP="$ART/$F/$APP_NAME.$EXT"
  OUT="$PKGDIR/FusionEQ-${F}.pkg"
  # --ownership recommended: 系统插件目录统一 root:wheel，无需 sudo
  pkgbuild --component "$COMP" \
           --install-location "$LOC" \
           --ownership recommended \
           --identifier "$ID" \
           --version "$VERSION" \
           "$OUT"
  PKGLIST+=("$OUT")
  echo "    [+] $F -> $LOC"
done

echo "[*] productbuild 合并 ..."
ARGS=()
for p in "${PKGLIST[@]}"; do ARGS+=(--package "$p"); done
rm -f "$OUT_PKG"
productbuild "${ARGS[@]}" "$OUT_PKG"

# ---- 6. PKG 签名（Developer ID Installer） ----
echo "[*] productsign ..."
productsign --sign "$INSTALLER_IDENTITY" "$OUT_PKG" "$OUT_PKG.signed"
mv "$OUT_PKG.signed" "$OUT_PKG"

# ---- 7. 公证 + 装订 ----
echo "[*] notarytool submit（最长 30 分钟）..."
xcrun notarytool submit "$OUT_PKG" \
  --apple-id "$APPLE_ID" \
  --password "$APPLE_APP_SPECIFIC_PASSWORD" \
  --team-id "$APPLE_TEAM_ID" \
  --wait --timeout 30m
echo "[*] stapler staple ..."
xcrun stapler staple "$OUT_PKG"
echo "[*] spctl Gatekeeper 校验（应显示 accepted / Notarized Developer ID）..."
spctl -a -vvv -t install "$OUT_PKG"

# ---- 8. 最终校验和 ----
shasum -a 256 "$OUT_PKG" > "$OUT_PKG.sha256"
echo
echo "=== 完成 ==="
echo "  PKG    : $OUT_PKG"
echo "  大小   : $(stat -f%z "$OUT_PKG") B"
echo "  SHA256 : $(cat "$OUT_PKG.sha256")"
echo "  请连同名 .sha256 一起发回，由 Windows 端上传官网并精确替换下载入口。"
echo "  ★ 不要再用 CI 的旧 PKG（那个 AAX 没有 PACE 签名）。"
