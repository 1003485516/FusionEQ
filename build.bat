@echo off
REM ===========================================================================
REM   Fusion EQ  ??  Windows ???????
REM
REM   ????: build\FusionEQ_artefacts\Release\VST3\Fusion EQ.vst3
REM ===========================================================================
setlocal enabledelayedexpansion

REM ---- ??????¦Ë??????? E ???????????????? E:\FusionEQ?????????????? C ?? ----
set "TOOLS=E:\FusionEQ\tools"
set "SDK=E:\FusionEQ\sdk"
if not exist "%TOOLS%\cmake-3.31.6-windows-x86_64\bin\cmake.exe" set "TOOLS=C:\Users\Administrator\tools"
if not exist "%SDK%\JUCE-7.0.12" if not exist "%SDK%\JUCE" set "SDK=C:\Users\Administrator\sdk"

set "CMAKE=%TOOLS%\cmake-3.31.6-windows-x86_64\bin\cmake.exe"
set "NINJA=%TOOLS%\ninja.exe"

REM ---- ??? CMake ----
if not exist "%CMAKE%" (
    echo [X] ????? CMake: %CMAKE%
    exit /b 1
)
if not exist "%NINJA%" (
    echo [X] ????? Ninja: %NINJA%
    exit /b 1
)

REM ---- ??¦Ë MinGW ????????llvm-mingw ?? w64devkit??----
set "MINGW="
for /d %%d in ("%TOOLS%\llvm-mingw-*") do set "MINGW=%%d\bin"
if not defined MINGW (
    for /d %%d in ("%TOOLS%\w64devkit*") do set "MINGW=%%d\bin"
)
if not defined MINGW (
    echo [X] ????? MinGW ??????????? llvm-mingw ?? w64devkit ????? %TOOLS%
    exit /b 1
)

if exist "%MINGW%\clang++.exe" (
    set "CC_BIN=%MINGW%\clang.exe"
    set "CXX_BIN=%MINGW%\clang++.exe"
    echo [*] ??????: clang   ^(%MINGW%^)
) else if exist "%MINGW%\g++.exe" (
    set "CC_BIN=%MINGW%\gcc.exe"
    set "CXX_BIN=%MINGW%\g++.exe"
    echo [*] ??????: gcc     ^(%MINGW%^)
) else (
    echo [X] %MINGW% ?????? clang++ ???? g++
    exit /b 1
)

REM ---- ??¦Ë JUCE ----
set "JUCE_DIR="
for /d %%d in ("%SDK%\JUCE-7*") do set "JUCE_DIR=%%d"
if not defined JUCE_DIR (
    for /d %%d in ("%SDK%\JUCE*") do set "JUCE_DIR=%%d"
)
if not defined JUCE_DIR (
    echo [X] ????? JUCE???????? %SDK%
    exit /b 1
)
echo [*] JUCE    : %JUCE_DIR%

set "PATH=%MINGW%;%TOOLS%;%PATH%"

echo.
echo === ???? ===
"%CMAKE%" -S "%~dp0." -B "%~dp0build" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_C_COMPILER="%CC_BIN%" ^
    -DCMAKE_CXX_COMPILER="%CXX_BIN%" ^
    -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
    -DJUCE_DIR="%JUCE_DIR%"
if errorlevel 1 ( echo [X] ??????? & exit /b 1 )

echo.
echo === ???? ===
"%CMAKE%" --build "%~dp0build" --config Release
if errorlevel 1 ( echo [X] ??????? & exit /b 1 )

echo.
echo === ??? ===
dir "%~dp0build\FusionEQ_artefacts\Release\VST3" 2>nul
