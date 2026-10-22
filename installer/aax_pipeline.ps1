# Fusion EQ - AAX 一键流水线：找/解压 SDK -> 编译 AAX -> 并入构建目录 -> 重打安装包
param([switch]$NoPackage)

$ErrorActionPreference = 'Stop'
$proj    = Split-Path -Parent $PSScriptRoot
$sdkDir  = 'E:\FusionEQ\sdk'
$juceDir = 'E:\FusionEQ\sdk\JUCE-7.0.12'
$cmake   = 'E:\FusionEQ\tools\cmake-3.31.6-windows-x86_64\bin\cmake.exe'
$iscc    = 'E:\FusionEQ\tools\InnoSetup\ISCC.exe'
$buildAax    = Join-Path $proj 'build_aax'
$mainRelease = Join-Path $proj 'build\FusionEQ_artefacts\Release'

function Write-Step($msg) { Write-Host ''; Write-Host "=== $msg ===" -ForegroundColor Cyan }

try {
    # ---- 1) 自动解压 zip（文件名含 aax）----
    Write-Step '查找 AAX SDK'
    $zips = Get-ChildItem $sdkDir -Filter '*.zip' -ErrorAction SilentlyContinue | Where-Object { $_.Name -match 'aax' }
    foreach ($z in $zips) {
        $dest = Join-Path $sdkDir $z.BaseName
        if (-not (Test-Path $dest)) {
            Write-Host "  解压 $($z.Name) ..."
            Expand-Archive -LiteralPath $z.FullName -DestinationPath $dest -Force
        }
    }

    # ---- 2) 定位 SDK 根（找含 Interfaces\AAX.h 的目录，最多向下两层）----
    $roots = @()
    $p = Join-Path $sdkDir 'AAX_SDK'
    if (Test-Path $p) { $roots += $p }
    $roots += Get-ChildItem $sdkDir -Directory -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName }

    $sdk = $null
    foreach ($lvl1 in $roots) {
        if (Test-Path (Join-Path $lvl1 'Interfaces\AAX.h')) { $sdk = $lvl1; break }
        foreach ($lvl2 in (Get-ChildItem $lvl1 -Directory -ErrorAction SilentlyContinue)) {
            if (Test-Path (Join-Path $lvl2.FullName 'Interfaces\AAX.h')) { $sdk = $lvl2.FullName; break }
        }
        if ($sdk) { break }
    }

    if (-not $sdk) {
        Write-Host ''
        Write-Host '[X] 没找到 AAX SDK。' -ForegroundColor Red
        Write-Host '    你只需要做这一步：'
        Write-Host '      1) 到 https://developer.avid.com/aax 登录 Avid 账号'
        Write-Host '      2) 点 Download Evaluation Toolkit，接受协议'
        Write-Host '      3) 在 my.avid.com/products/cppsdk 下载 AAX SDK（Windows 版 zip）'
        Write-Host '      4) 把 zip 直接丢进  E:\FusionEQ\sdk  文件夹（文件名含 aax 即可）'
        Write-Host '      5) 重新运行本脚本，解压/编译/打包全自动完成'
        exit 1
    }
    Write-Host "  SDK: $sdk"

    # ---- 3) CMake 配置 + 编译（MSVC）----
    # 注意：本环境沙箱禁用了 reg.exe，而 vcvars64.bat 依赖 reg 查询 Windows SDK
    # 安装路径，会导致环境只加载一半（rc.exe/mt.exe 找不到，编译测试失败）。
    # 因此这里不调用 vcvars，改为手动拼装 MSVC + Windows SDK 环境变量，
    # 版本号动态探测，不写死。
    Write-Step '拼装 MSVC 编译环境'
    $vsRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
    if (-not (Test-Path $vsRoot)) { throw "找不到 Visual Studio BuildTools：$vsRoot" }

    $msvcVer = Get-ChildItem (Join-Path $vsRoot 'VC\Tools\MSVC') -Directory |
               Sort-Object Name -Descending | Select-Object -First 1
    if (-not $msvcVer) { throw '找不到 MSVC 工具集（需安装 VS2022 C++ 工作负载）' }

    $sdkInc = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\Include' -Directory |
              Sort-Object { [Version]($_.Name) } -Descending | Select-Object -First 1
    if (-not $sdkInc) { throw '找不到 Windows SDK（需安装 Windows 10/11 SDK）' }
    $sdkVer = $sdkInc.Name

    $bin64  = Join-Path $msvcVer.FullName 'bin\Hostx64\x64'
    $sdkBin = "C:\Program Files (x86)\Windows Kits\10\bin\$sdkVer\x64"
    foreach ($d in @($bin64, $sdkBin)) { if (-not (Test-Path $d)) { throw "缺少目录：$d" } }

    $env:PATH = "$bin64;$sdkBin;$env:PATH"
    $env:INCLUDE = "$($msvcVer.FullName)\include;" +
        "C:\Program Files (x86)\Windows Kits\10\Include\$sdkVer\ucrt;" +
        "C:\Program Files (x86)\Windows Kits\10\Include\$sdkVer\um;" +
        "C:\Program Files (x86)\Windows Kits\10\Include\$sdkVer\shared;" +
        "C:\Program Files (x86)\Windows Kits\10\Include\$sdkVer\winrt;" +
        "C:\Program Files (x86)\Windows Kits\10\Include\$sdkVer\cppwinrt"
    $env:LIB = "$($msvcVer.FullName)\lib\x64;" +
        "C:\Program Files (x86)\Windows Kits\10\Lib\$sdkVer\ucrt\x64;" +
        "C:\Program Files (x86)\Windows Kits\10\Lib\$sdkVer\um\x64"
    Write-Host "  MSVC: $($msvcVer.Name)    Windows SDK: $sdkVer"

    $ninja = 'E:\FusionEQ\tools\ninja.exe'
    if (-not (Test-Path $ninja)) { throw "找不到 ninja：$ninja" }

    Write-Step '配置（MSVC + Ninja）'
    if (Test-Path $buildAax) { Remove-Item $buildAax -Recurse -Force }
    & $cmake -S $proj -B $buildAax -G Ninja -DCMAKE_BUILD_TYPE=Release `
        "-DCMAKE_MAKE_PROGRAM=$ninja" "-DJUCE_DIR=$juceDir" "-DAAX_SDK_PATH=$sdk"
    if ($LASTEXITCODE -ne 0) { throw 'CMake 配置失败（详见上方输出）' }

    Write-Step '编译 AAX（FusionEQ_AAX）'
    & $cmake --build $buildAax --config Release --target FusionEQ_AAX
    if ($LASTEXITCODE -ne 0) { throw 'AAX 编译失败（详见上方输出）' }

    $aaxOut = Join-Path $buildAax 'FusionEQ_artefacts\Release\AAX'
    if (-not (Test-Path (Join-Path $aaxOut 'Fusion EQ.aaxplugin'))) { throw "未找到 AAX 产物：$aaxOut" }

    # ---- 4) 把 AAX 产物并入主构建目录（安装包从这里取）----
    Write-Step '并入主构建目录'
    $dstAAX = Join-Path $mainRelease 'AAX'
    # 必须先删目标再复制：目标已存在时 Copy-Item 会把源目录塞进去一层
    # （AAX\AAX\Fusion EQ.aaxplugin），安装包会打进上一轮的旧文件。
    if (Test-Path $dstAAX) { Remove-Item $dstAAX -Recurse -Force }
    Copy-Item $aaxOut $dstAAX -Recurse -Force
    Write-Host "  已复制到 $dstAAX"

    if ($NoPackage) {
        Write-Host ''
        Write-Host '[完成] AAX 编译成功（按要求未重新打包）。' -ForegroundColor Green
        exit 0
    }

    # ---- 5) 重新打 Windows 安装包 ----
    Write-Step '重新打 Windows 安装包'
    & $iscc "/DBuildDir=$mainRelease" (Join-Path $PSScriptRoot 'FusionEQ.iss')
    if ($LASTEXITCODE -ne 0) { throw '安装包编译失败' }

    Write-Host ''
    Write-Host '[完成] 安装包已生成：E:\FusionEQ\安装包\FusionEQ-1.0.2-Setup.exe' -ForegroundColor Green
    Write-Host '       安装界面里现在有 VST3 和 AAX（Pro Tools）两个可勾选组件。'
}
catch {
    Write-Host ''
    Write-Host "[失败] $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
