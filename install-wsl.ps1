param(
    [string]$Distro = "Ubuntu",
    [string]$BuildDir = "build-wsl-native",
    [string]$Prefix = "~/.local",
    [switch]$InstallPackages,
    [switch]$SkipGuiSmoke
)

$ErrorActionPreference = "Stop"

function Test-CommandAvailable {
    param([string]$Name)
    return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

function ConvertTo-WslPath {
    param([string]$Path)
    $resolved = (Resolve-Path -LiteralPath $Path).Path
    if ($resolved -notmatch "^[A-Za-z]:\\") {
        throw "Only drive-letter Windows paths can be mapped to WSL: $resolved"
    }
    $drive = $resolved.Substring(0, 1).ToLowerInvariant()
    $rest = $resolved.Substring(3) -replace "\\", "/"
    return "/mnt/$drive/$rest"
}

function Quote-Bash {
    param([string]$Value)
    return "'" + ($Value -replace "'", "'\''") + "'"
}

if (-not (Test-CommandAvailable "wsl.exe")) {
    throw "wsl.exe not found. Install WSL first."
}

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoWsl = ConvertTo-WslPath $repoRoot
if ($Prefix -match "^[A-Za-z]:[\\/]" -and $env:USERPROFILE) {
    $expandedUserLocal = Join-Path $env:USERPROFILE ".local"
    if ([System.IO.Path]::GetFullPath($Prefix).TrimEnd("\", "/") -ieq [System.IO.Path]::GetFullPath($expandedUserLocal).TrimEnd("\", "/")) {
        $Prefix = "~/.local"
    } else {
        throw "Prefix must be a WSL path, for example ~/.local or /usr/local."
    }
}

$installPackagesValue = if ($InstallPackages) { "1" } else { "0" }
$skipGuiSmokeValue = if ($SkipGuiSmoke) { "1" } else { "0" }

$bashTemplate = @'
set -euo pipefail

repo=__REPO__
build_dir=__BUILD_DIR__
prefix=__PREFIX__
install_packages=__INSTALL_PACKAGES__
skip_gui_smoke=__SKIP_GUI_SMOKE__

case "$prefix" in
    "~") prefix="$HOME" ;;
    "~/"*) prefix="$HOME/${prefix#"~/"}" ;;
esac

required_commands="cmake g++ make"
missing_commands=""
for command_name in $required_commands; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        missing_commands="$missing_commands $command_name"
    fi
done
if [ -n "$missing_commands" ]; then
    echo "ERROR missing WSL commands:$missing_commands" >&2
    echo "Install with: sudo apt install build-essential cmake" >&2
    exit 1
fi

required_packages="libx11-dev libxft-dev libxext-dev libxinerama-dev libxcursor-dev libxrender-dev libxfixes-dev libpng-dev libjpeg-dev zlib1g-dev libupnp-dev"
missing_packages=""
if command -v dpkg >/dev/null 2>&1; then
    for package_name in $required_packages; do
        if ! dpkg -s "$package_name" >/dev/null 2>&1; then
            missing_packages="$missing_packages $package_name"
        fi
    done
fi

if [ -n "$missing_packages" ]; then
    if [ "$install_packages" = "1" ]; then
        sudo apt-get update
        sudo apt-get install -y build-essential cmake $required_packages
    else
        echo "ERROR missing native GUI build packages:$missing_packages" >&2
        echo "Rerun with -InstallPackages, or run: sudo apt install build-essential cmake $required_packages" >&2
        exit 1
    fi
fi

cd "$repo"
cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" -DDLNA_ENABLE_FLTK_GUI=ON
cmake --build "$build_dir" -j"$(nproc)"
cmake --install "$build_dir"

test -x "$prefix/bin/dlna-server"
test -x "$prefix/bin/dlna-server-gui"
test -x "$prefix/bin/dlna-server-gui-bin"
"$prefix/bin/dlna-server" --help >/tmp/dlna-server-help.out 2>&1

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$prefix/share/applications" >/dev/null 2>&1 || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t "$prefix/share/icons/hicolor" >/dev/null 2>&1 || true
fi

if [ "$skip_gui_smoke" != "1" ] && { [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ]; }; then
    set +e
    timeout 5s "$prefix/bin/dlna-server-gui" >/tmp/dlna-wsl-install-gui.out 2>/tmp/dlna-wsl-install-gui.err
    code=$?
    set -e
    if [ "$code" != "0" ] && [ "$code" != "124" ]; then
        cat /tmp/dlna-wsl-install-gui.err >&2 || true
        exit "$code"
    fi
    echo "PASS WSL GUI smoke"
fi

echo "Installed dlna-server to $prefix"
'@

$bashScript = $bashTemplate.
    Replace("__REPO__", (Quote-Bash $repoWsl)).
    Replace("__BUILD_DIR__", (Quote-Bash $BuildDir)).
    Replace("__PREFIX__", (Quote-Bash $Prefix)).
    Replace("__INSTALL_PACKAGES__", (Quote-Bash $installPackagesValue)).
    Replace("__SKIP_GUI_SMOKE__", (Quote-Bash $skipGuiSmokeValue))

$bashScript -replace "`r", "" | wsl.exe -d $Distro bash
if ($LASTEXITCODE -ne 0) {
    throw "WSL install failed."
}
