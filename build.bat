@echo off
REM ===========================================================================
REM   Fusion EQ  ·  Windows 一键编译
REM
REM   产物: build\FusionEQ_artefacts\Release\VST3\Fusion EQ.vst3
REM ===========================================================================
setlocal enabledelayedexpansion

set "TOOLS=C:\Users\Administrator\tools"
set "SDK=C:\Users\Administrator\sdk"

set "CMAKE=%TOOLS%\cmake-3.31.6-windows-x86_64\bin\cmake.exe"
set "NINJA=%TOOLS%\ninja.exe"

REM ---- 检查 CMake ----
if not exist "%CMAKE%" (
    echo [X] 找不到 CMake: %CMAKE%
    exit /b 1
)
if not exist "%NINJA%" (
    echo [X] 找不到 Ninja: %NINJA%
    exit /b 1
)

REM ---- 定位 MinGW 工具链（llvm-mingw 或 w64devkit）----
set "MINGW="
for /d %%d in ("%TOOLS%\llvm-mingw-*") do set "MINGW=%%d\bin"
if not defined MINGW (
    for /d %%d in ("%TOOLS%\w64devkit*") do set "MINGW=%%d\bin"
)
if not defined MINGW (
    echo [X] 找不到 MinGW 工具链，请把 llvm-mingw 或 w64devkit 解压到 %TOOLS%
    exit /b 1
)

if exist "%MINGW%\clang++.exe" (
    set "CC_BIN=%MINGW%\clang.exe"
    set "CXX_BIN=%MINGW%\clang++.exe"
    echo [*] 编译器: clang   ^(%MINGW%^)
) else if exist "%MINGW%\g++.exe" (
    set "CC_BIN=%MINGW%\gcc.exe"
    set "CXX_BIN=%MINGW%\g++.exe"
    echo [*] 编译器: gcc     ^(%MINGW%^)
) else (
    echo [X] %MINGW% 下既没有 clang++ 也没有 g++
    exit /b 1
)

REM ---- 定位 JUCE ----
set "JUCE_DIR="
for /d %%d in ("%SDK%\JUCE-7*") do set "JUCE_DIR=%%d"
if not defined JUCE_DIR (
    for /d %%d in ("%SDK%\JUCE*") do set "JUCE_DIR=%%d"
)
if not defined JUCE_DIR (
    echo [X] 找不到 JUCE，请解压到 %SDK%
    exit /b 1
)
echo [*] JUCE    : %JUCE_DIR%

set "PATH=%MINGW%;%TOOLS%;%PATH%"

echo.
echo === 配置 ===
"%CMAKE%" -S "%~dp0." -B "%~dp0build" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_C_COMPILER="%CC_BIN%" ^
    -DCMAKE_CXX_COMPILER="%CXX_BIN%" ^
    -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
    -DJUCE_DIR="%JUCE_DIR%"
if errorlevel 1 ( echo [X] 配置失败 & exit /b 1 )

echo.
echo === 编译 ===
"%CMAKE%" --build "%~dp0build" --config Release
if errorlevel 1 ( echo [X] 编译失败 & exit /b 1 )

echo.
echo === 完成 ===
dir "%~dp0build\FusionEQ_artefacts\Release\VST3" 2>nul
