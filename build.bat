@echo off
setlocal

cd /d "%~dp0"

set "BUILD_X86=0"
set "BUILD_X64=0"
set "BUILD_LINUX=0"
set "INSTALL_LINUX=0"
set "RELEASE=0"
set "NOTES=0"
set "UPDATE="
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
if /I "%~1"=="--release" (
    set "RELEASE=1"
    shift
    goto :parse_args
)
if /I "%~1"=="--notes" (
    set "NOTES=1"
    shift
    goto :parse_args
)
if /I "%~1"=="--update" (
    set "UPDATE=%~2"
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
:: Handle --update=TAG format
echo "%~1" | findstr /I "--update=" >nul
if not errorlevel 1 (
    for /f "tokens=2 delims==" %%i in ("%~1") do set "UPDATE=%%i"
    shift
    goto :parse_args
)
shift
goto :parse_args

:done_parse

:: Default if no platform specified is x64
set "HAS_WIN_ARCH=0"
if "%BUILD_X86%"=="1" set "HAS_WIN_ARCH=1"
if "%BUILD_X64%"=="1" set "HAS_WIN_ARCH=1"

if "%RELEASE%"=="1" (
    echo Releasing assets...
    set "REL_ARGS="
    if "%NOTES%"=="1" set "REL_ARGS=-Notes"
    if not "%UPDATE%"=="" set "REL_ARGS=%REL_ARGS% -Update %UPDATE%"
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\release-windows.ps1" %REL_ARGS%
    exit /b %ERRORLEVEL%
)

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
