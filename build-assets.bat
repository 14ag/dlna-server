@echo off
setlocal

cd /d "%~dp0"

if "%~1"=="/?" goto :usage
if /I "%~1"=="-h" goto :usage
if /I "%~1"=="--help" goto :usage

where powershell.exe >nul 2>nul
if errorlevel 1 (
    echo ERROR: powershell.exe not found.
    exit /b 1
)

where cmake.exe >nul 2>nul
if errorlevel 1 (
    echo ERROR: cmake.exe not found.
    exit /b 1
)

where wsl.exe >nul 2>nul
if errorlevel 1 (
    echo ERROR: wsl.exe not found. Linux assets require WSL.
    exit /b 1
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-release-assets.ps1" %*
exit /b %ERRORLEVEL%

:usage
echo Build DLNA Server release assets into .\output
echo.
echo Usage:
echo   build-assets.bat
echo   build-assets.bat -Version 1.3.0
echo   build-assets.bat -Version 1.3.0 -WslDistro Ubuntu
echo.
echo Assets:
echo   Windows x64 zip, Windows x86 zip, Linux desktop packages, SHA256SUMS.txt
exit /b 0
