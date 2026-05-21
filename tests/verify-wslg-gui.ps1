param(
    [string]$Distro = "Ubuntu",
    [string]$BuildDir = "build-wslg-gui",
    [string]$InstallDir = "output-wslg-gui"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$repoWsl = "/mnt/c/" + ($repoRoot.Substring(3) -replace "\\", "/")

function Invoke-Wsl {
    param([string]$Script)
    $Script -replace "`r", "" | wsl.exe -d $Distro bash
    if ($LASTEXITCODE -ne 0) {
        throw "WSL command failed."
    }
}

$requiredPackages = @(
    "libx11-dev",
    "libxft-dev",
    "libxext-dev",
    "libxinerama-dev",
    "libxcursor-dev",
    "libxrender-dev",
    "libxfixes-dev",
    "libpng-dev",
    "libjpeg-dev",
    "zlib1g-dev"
)

$packageList = $requiredPackages -join " "
$checkScript = @"
set -eu
missing=""
for package in $packageList; do
    if ! dpkg -s "`$package" >/dev/null 2>&1; then
        missing="`$missing `$package"
    fi
done
if [ -n "`$missing" ]; then
    echo "WARN missing native GUI build packages:`$missing"
    echo "Install with: sudo apt install build-essential cmake $packageList"
    exit 0
fi
cd "$repoWsl"
cmake -S . -B "$BuildDir" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$repoWsl/$InstallDir" -DDLNA_ENABLE_FLTK_GUI=ON
cmake --build "$BuildDir" -j`$(nproc)
cmake --install "$BuildDir"
test -x "$InstallDir/bin/dlna-server"
test -x "$InstallDir/bin/dlna-server-gui"
test -n "`${DISPLAY:-}" -o -n "`${WAYLAND_DISPLAY:-}"
timeout 3s "$InstallDir/bin/dlna-server-gui" >/tmp/dlna-wslg-gui.out 2>/tmp/dlna-wslg-gui.err || code=`$?
code=`${code:-0}
if [ "`$code" != "0" ] && [ "`$code" != "124" ]; then
    cat /tmp/dlna-wslg-gui.err >&2 || true
    exit "`$code"
fi
echo "PASS WSLg native GUI launch smoke"
"@

Invoke-Wsl $checkScript
