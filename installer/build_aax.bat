@echo off
title Fusion EQ - 仅编译 AAX（不打包）
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0aax_pipeline.ps1" -NoPackage
echo.
pause
