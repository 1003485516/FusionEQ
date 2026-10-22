@echo off
REM ===========================================================================
REM  Fusion EQ - Windows code signing (Authenticode)
REM
REM  Signs the VST3 binary, AAX binary, and the installer Setup.exe so that
REM  Windows SmartScreen and Pro Tools AAX validation pass for end users.
REM  Without this, Pro Tools (Win) refuses to load the AAX, and Windows shows
REM  a SmartScreen warning for the installer.
REM
REM  Requires a code-signing certificate (.pfx) from a trusted CA
REM  (Sectigo / DigiCert OV, or SSL.com eSigner for CI-friendly signing).
REM
REM  Usage:
REM    set FUSION_WIN_PFX=C:\path\to\cert.pfx
REM    set FUSION_WIN_PASS=your_cert_password
REM    sign_windows.bat
REM  Optional (to also sign the installer exe, run AFTER ISCC):
REM    set FUSION_SETUP_PATH=E:\FusionEQ\安装包\FusionEQ-1.0.3-Setup.exe
REM    sign_windows.bat
REM ===========================================================================
setlocal enabledelayedexpansion

REM ---- locate signtool (Windows SDK) ----
set "SIGNTOOL=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe"
if not exist "%SIGNTOOL%" (
    for /f "delims=" %%i in ('dir /b /s "C:\Program Files (x86)\Windows Kits\10\bin\signtool.exe" 2^>nul') do set "SIGNTOOL=%%i"
)
if not exist "%SIGNTOOL%" (
    echo [X] signtool.exe not found
    exit /b 1
)

if not defined FUSION_WIN_PFX (
    echo [X] FUSION_WIN_PFX not set (path to .pfx certificate)
    exit /b 1
)
if not exist "%FUSION_WIN_PFX%" (
    echo [X] PFX not found: %FUSION_WIN_PFX%
    exit /b 1
)
if not defined FUSION_WIN_PASS (
    echo [X] FUSION_WIN_PASS not set (certificate password)
    exit /b 1
)

set "TS=http://timestamp.digicert.com"
set "ART=%~dp0build\FusionEQ_artefacts\Release"

REM ---- VST3 binary ----
set "VST3=%ART%\VST3\Fusion EQ.vst3\Contents\x86_64-win\Fusion EQ.vst3"
if exist "%VST3%" (
    echo [*] Signing VST3: %VST3%
    "%SIGNTOOL%" sign /f "%FUSION_WIN_PFX%" /p "%FUSION_WIN_PASS%" /fd sha256 /tr "%TS%" /td sha256 "%VST3%"
) else (
    echo [-] VST3 binary not found, skip
)

REM ---- AAX binary (Pro Tools) ----
set "AAX=%ART%\AAX\Fusion EQ.aaxplugin\Contents\x64\Fusion EQ.aaxplugin"
if exist "%AAX%" (
    echo [*] Signing AAX: %AAX%
    "%SIGNTOOL%" sign /f "%FUSION_WIN_PFX%" /p "%FUSION_WIN_PASS%" /fd sha256 /tr "%TS%" /td sha256 "%AAX%"
) else (
    echo [-] AAX binary not found, skip
)

REM ---- Installer Setup.exe (only if path provided, run after ISCC) ----
if defined FUSION_SETUP_PATH if exist "%FUSION_SETUP_PATH%" (
    echo [*] Signing Setup: %FUSION_SETUP_PATH%
    "%SIGNTOOL%" sign /f "%FUSION_WIN_PFX%" /p "%FUSION_WIN_PASS%" /fd sha256 /tr "%TS%" /td sha256 "%FUSION_SETUP_PATH%"
) else (
    echo [-] FUSION_SETUP_PATH not set, skip Setup.exe (sign it after ISCC)
)

echo.
echo === Done signing ===
endlocal
