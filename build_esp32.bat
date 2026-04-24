@echo off
rem =============================================================================
rem build_esp32.bat — 调用同目录 build_esp32.ps1（ESP-IDF 编译 / 激活）
rem
rem 用法：
rem   build_esp32.bat              完整编译（idf.py build）
rem   build_esp32.bat -ActivateOnly   只激活环境并进入工程目录后退出（见 ps1 内说明）
rem
rem 配置在 idf-env.local.ps1 或 %%USERPROFILE%%\.esp-idf-build.ps1，详见 idf-env.example.ps1
rem 详细说明请打开 build_esp32.ps1 文件头部注释（Get-Help 风格使用说明）。
rem =============================================================================
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_esp32.ps1" %*
exit /b %ERRORLEVEL%
