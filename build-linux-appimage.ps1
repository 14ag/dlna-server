param(
    [string]$Config = "Release",
    [string]$BuildDir = "build_output",
    [string]$OutputDir = "output",
    [string]$AppDir = "output/DLNA_Server.AppDir",
    [string]$LinuxDeployPath = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$isLinuxHost = $IsLinux

if (-not $isLinuxHost) {
    throw "AppImage build requires Linux. Use WSL, a Linux VM, or CI."
}

function Resolve-WorkspacePath {
    param([string]$PathToCheck)

    $fullPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $PathToCheck))
    $fullRoot = [System.IO.Path]::GetFullPath($repoRoot).TrimEnd('\', '/')

    if (-not $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing path outside workspace: $fullPath"
    }

    return $fullPath
}

function Find-LinuxDeploy {
    param([string]$RequestedPath)

    if ($RequestedPath) {
        $resolved = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $RequestedPath))
        if (Test-Path -LiteralPath $resolved) {
            return $resolved
        }
        throw "linuxdeploy not found at: $resolved"
    }

    if ($env:LINUXDEPLOY_PATH -and (Test-Path -LiteralPath $env:LINUXDEPLOY_PATH)) {
        return $env:LINUXDEPLOY_PATH
    }

    foreach ($name in @("linuxdeploy-x86_64.AppImage", "linuxdeploy")) {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if ($cmd) {
            return $cmd.Source
        }
    }

    throw "linuxdeploy missing. Install linuxdeploy-x86_64.AppImage or set LINUXDEPLOY_PATH."
}

$outputPath = Resolve-WorkspacePath $OutputDir
$appDirPath = Resolve-WorkspacePath $AppDir
$buildPath = Resolve-WorkspacePath $BuildDir

& cmake -S $repoRoot -B $buildPath -DCMAKE_BUILD_TYPE=$Config -DCMAKE_INSTALL_PREFIX=$outputPath -DDLNA_ENABLE_FLTK_GUI=ON
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed."
}

& cmake --build $buildPath --config $Config
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed."
}

if (Test-Path -LiteralPath $outputPath) {
    Get-ChildItem -LiteralPath $outputPath -Force | Remove-Item -Recurse -Force
} else {
    New-Item -ItemType Directory -Path $outputPath | Out-Null
}

& cmake --install $buildPath --config $Config --prefix $outputPath
if ($LASTEXITCODE -ne 0) {
    throw "CMake install failed."
}

& (Join-Path $repoRoot "build-linux-appdir.ps1") -InstallDir $OutputDir -AppDir $AppDir
if ($LASTEXITCODE -ne 0) {
    throw "build-linux-appdir.ps1 failed."
}

$linuxDeploy = Find-LinuxDeploy $LinuxDeployPath
chmod +x $linuxDeploy
chmod +x (Join-Path $appDirPath "AppRun")

Push-Location $outputPath
try {
    & $linuxDeploy --appdir $appDirPath --output appimage
    if ($LASTEXITCODE -ne 0) {
        throw "linuxdeploy AppImage build failed."
    }
}
finally {
    Pop-Location
}

$appImage = Get-ChildItem -LiteralPath $outputPath -Filter "*.AppImage" -File |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if (-not $appImage) {
    throw "AppImage artifact missing in $outputPath"
}

Write-Host "AppImage ready: $($appImage.FullName)"
