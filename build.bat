@echo off
setlocal

cd /d "%~dp0"

set "BUILD_X86=0"
set "BUILD_X64=0"
set "BUILD_LINUX=0"
set "INSTALL_LINUX=0"
set "VERSION_TAG="

:parse_args
if "%~1"=="" goto :done_parse
if /I "%~1"=="--x86" (
    set "BUILD_X86=1"
    shift
    goto :parse_args
)
if /I "%~1"=="--x64" (
    set "BUILD_X64=1"
    shift
    goto :parse_args
)
if /I "%~1"=="--linux" (
    set "BUILD_LINUX=1"
    shift
    goto :parse_args
)
if /I "%~1"=="--install" (
    set "INSTALL_LINUX=1"
    shift
    goto :parse_args
)
if /I "%~1"=="--build" (
    set "DLNA_BUILD=%~2"
    shift
    shift
    goto :parse_args
)
if /I "%~1"=="--version" (
    set "VERSION_TAG=%~2"
    shift
    shift
    goto :parse_args
)
set "ARG=%~1"
if /I "%ARG:~0,10%"=="--version=" (
    set "VERSION_TAG=%ARG:~10%"
    shift
    goto :parse_args
)
shift
goto :parse_args

:done_parse

if "%INSTALL_LINUX%"=="1" (
    echo Installing Linux app via WSL...
    wsl.exe -d Ubuntu -- bash -lc 'bash scripts/install_linux.sh'
    exit /b %ERRORLEVEL%
)

if "%VERSION_TAG%"=="" (
    for /f "usebackq delims=" %%v in (`python "%~dp0scripts\version.py"`) do set "VERSION_TAG=%%v"
)
if "%VERSION_TAG%"=="" (
    echo Could not generate a version number.
    exit /b 1
)
echo Version: %VERSION_TAG%
if "%BUILD_LINUX%"=="1" (
    echo Building Linux assets via WSL...
    wsl.exe -d Ubuntu -- bash -lc 'DLNA_SUDO_PASSWORD=" " DLNA_VERSION_TAG='%VERSION_TAG%' bash scripts/build_linux.sh'
    exit /b %ERRORLEVEL%
)

:: Normal Windows Build
set "ARCH_ARG=both"
if "%BUILD_X86%"=="1" if "%BUILD_X64%"=="0" set "ARCH_ARG=Win32"
if "%BUILD_X64%"=="1" if "%BUILD_X86%"=="0" set "ARCH_ARG=x64"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-windows.ps1" -Arch %ARCH_ARG% -Version "%VERSION_TAG%"
exit /b %ERRORLEVEL%
