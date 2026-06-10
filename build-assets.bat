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

if "%~1"=="" goto :check_wsl
echo %* | findstr /I "linux" >nul 2>nul
if errorlevel 1 goto :run_build

:check_wsl
where wsl.exe >nul 2>nul
if errorlevel 1 (
    echo ERROR: wsl.exe missing. Linux assets require WSL.
    exit /b 1
)

:run_build
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-release-assets.ps1" %*
exit /b %ERRORLEVEL%

:usage
echo Build DLNA Server release assets into .\output
echo.
echo Usage:
echo   build-assets.bat
echo   build-assets.bat -Version 1.3.0
echo   build-assets.bat -Version 1.3.0 -WslDistro Ubuntu
echo   build-assets.bat --platform winx64,linux --no-clean
echo.
echo Assets:
echo   output\winx64, output\winx86, output\linux, output\macos-x64, output\macos-arm64 by default
echo.
echo Options:
echo   --platform comma-separated platforms: winx64, winx86, linux, macos-x64, macos-arm64
echo   --no-clean keeps existing output platform folders before building
exit /b 0
