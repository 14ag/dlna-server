param(
    [string]$Version = "",
    [string]$WslDistro = "Ubuntu"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$output = Join-Path $repo "output"

function Invoke-NativeChecked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE"
    }
}

function Save-UrlIfMissing {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Url,
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (Test-Path -LiteralPath $Path) {
        $existing = Get-Item -LiteralPath $Path
        if ($existing.Length -gt 0) {
            return
        }
    }

    Write-Host "Downloading $Url..."
    $client = New-Object System.Net.WebClient
    try {
        $client.DownloadFile($Url, $Path)
    } finally {
        $client.Dispose()
    }
}

function Get-Sha256Hex {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $hashBytes = $sha256.ComputeHash($stream)
        return -join ($hashBytes | ForEach-Object { $_.ToString("x2") })
    } finally {
        $stream.Dispose()
        $sha256.Dispose()
    }
}

if (-not $Version) {
    $cmake = Get-Content -LiteralPath (Join-Path $repo "CMakeLists.txt") -Raw
    if ($cmake -notmatch "project\(dlna-server VERSION ([0-9.]+)\)") {
        throw "Could not read project version from CMakeLists.txt"
    }
    $Version = $Matches[1]
}

New-Item -ItemType Directory -Force -Path $output | Out-Null
Get-ChildItem -LiteralPath $output -File -ErrorAction SilentlyContinue |
    Where-Object {
        $_.Name -like "dlna-server-$Version-*" -or
        $_.Name -like "dlna-server_$($Version)_*" -or
        $_.Name -like "DLNA_Server-$Version-*" -or
        $_.Name -eq "SHA256SUMS.txt"
    } |
    Remove-Item -Force

function Invoke-CmakeBuild {
    param(
        [string]$BuildDir,
        [string]$Arch,
        [string]$InstallDir
    )

    $fullBuildDir = Join-Path $repo $BuildDir
    $fullInstallDir = Join-Path $repo $InstallDir
    $resolvedRepo = Resolve-Path -LiteralPath $repo
    if (Test-Path -LiteralPath $fullBuildDir) {
        $resolvedBuild = Resolve-Path -LiteralPath $fullBuildDir
        if (-not $resolvedBuild.Path.StartsWith($resolvedRepo.Path, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove outside repo: $($resolvedBuild.Path)"
        }
        Remove-Item -LiteralPath $resolvedBuild.Path -Recurse -Force
    }
    if (Test-Path -LiteralPath $fullInstallDir) {
        $resolvedInstall = Resolve-Path -LiteralPath $fullInstallDir
        if (-not $resolvedInstall.Path.StartsWith($resolvedRepo.Path, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove outside repo: $($resolvedInstall.Path)"
        }
        Remove-Item -LiteralPath $resolvedInstall.Path -Recurse -Force
    }

    Invoke-NativeChecked "cmake" @("-S", $repo, "-B", $fullBuildDir, "-A", $Arch, "-DCMAKE_INSTALL_PREFIX=$fullInstallDir")
    Invoke-NativeChecked "cmake" @("--build", $fullBuildDir, "--config", "Release", "--target", "install", "--", "/m")
}

Invoke-CmakeBuild -BuildDir "build-release-windows" -Arch "x64" -InstallDir "output/windows"
Invoke-CmakeBuild -BuildDir "build-release-windows-x86" -Arch "Win32" -InstallDir "output/windows-x86"

Compress-Archive -LiteralPath (Join-Path $output "windows/DLNA Server.exe") -DestinationPath (Join-Path $output "dlna-server-$Version-windows-x86_64.zip") -Force
Compress-Archive -LiteralPath (Join-Path $output "windows-x86/DLNA Server.exe") -DestinationPath (Join-Path $output "dlna-server-$Version-windows-x86.zip") -Force

$drive = $repo.Substring(0, 1).ToLowerInvariant()
$repoWsl = "/mnt/$drive" + $repo.Substring(2).Replace("\", "/")
$repoWslEscaped = $repoWsl.Replace("'", "'\''")
$toolsDir = Join-Path $output "tools"
$fltkTag = "release-1.4.5"
$fltkSource = Join-Path $toolsDir "fltk-$fltkTag"
$fltkArchive = Join-Path $toolsDir "fltk-$fltkTag.tar.gz"
if (-not (Test-Path -LiteralPath (Join-Path $fltkSource "CMakeLists.txt"))) {
    New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null
    $fltkUrl = "https://github.com/fltk/fltk/archive/refs/tags/$fltkTag.tar.gz"
    Save-UrlIfMissing $fltkUrl $fltkArchive

    $extractDir = Join-Path $toolsDir "fltk-extract"
    if (Test-Path -LiteralPath $extractDir) {
        Remove-Item -LiteralPath $extractDir -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $extractDir | Out-Null
    Invoke-NativeChecked "tar.exe" @("-xzf", $fltkArchive, "-C", $extractDir)

    $extractedSource = Get-ChildItem -LiteralPath $extractDir -Directory | Select-Object -First 1
    if (-not $extractedSource) {
        throw "Could not extract FLTK archive: $fltkArchive"
    }
    if (Test-Path -LiteralPath $fltkSource) {
        Remove-Item -LiteralPath $fltkSource -Recurse -Force
    }
    Move-Item -LiteralPath $extractedSource.FullName -Destination $fltkSource
    Remove-Item -LiteralPath $extractDir -Recurse -Force
}
$fltkWsl = "/mnt/$drive" + $fltkSource.Substring(2).Replace("\", "/")
$fltkWslEscaped = $fltkWsl.Replace("'", "'\''")
$linuxdeployPath = Join-Path $toolsDir "linuxdeploy-x86_64.AppImage"
$runtimePath = Join-Path $toolsDir "runtime-x86_64"
Save-UrlIfMissing "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" $linuxdeployPath
Save-UrlIfMissing "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64" $runtimePath
$linuxdeployWsl = "/mnt/$drive" + $linuxdeployPath.Substring(2).Replace("\", "/")
$runtimeWsl = "/mnt/$drive" + $runtimePath.Substring(2).Replace("\", "/")
$linuxdeployWslEscaped = $linuxdeployWsl.Replace("'", "'\''")
$runtimeWslEscaped = $runtimeWsl.Replace("'", "'\''")
$bashTemplate = @'
cd '__REPO_WSL__' &&
tr -d '\r' < scripts/build-linux-desktop-assets.sh > /tmp/dlna-server-build-linux-desktop-assets.sh &&
chmod +x /tmp/dlna-server-build-linux-desktop-assets.sh &&
DLNA_REPO_ROOT='__REPO_WSL__' DLNA_FLTK_SOURCE_DIR='__FLTK_WSL__' LINUXDEPLOY='__LINUXDEPLOY_WSL__' APPIMAGE_RUNTIME='__RUNTIME_WSL__' DLNA_SERVER_VERSION='__VERSION__' bash /tmp/dlna-server-build-linux-desktop-assets.sh
'@
$bashCommand = $bashTemplate.Replace("__REPO_WSL__", $repoWslEscaped)
$bashCommand = $bashCommand.Replace("__FLTK_WSL__", $fltkWslEscaped)
$bashCommand = $bashCommand.Replace("__LINUXDEPLOY_WSL__", $linuxdeployWslEscaped)
$bashCommand = $bashCommand.Replace("__RUNTIME_WSL__", $runtimeWslEscaped)
$bashCommand = $bashCommand.Replace("__VERSION__", $Version) -replace "`r?`n", " "
Invoke-NativeChecked "wsl.exe" @("-d", $WslDistro, "--", "bash", "-lc", $bashCommand)

$linuxAssets = Get-ChildItem -LiteralPath $output -File |
    Where-Object {
        $_.Name -like "dlna-server_$($Version)_*.deb" -or
        $_.Name -like "dlna-server-$Version-linux-*" -or
        $_.Name -like "DLNA_Server-$Version-*.AppImage"
    }
if (-not $linuxAssets) {
    throw "Linux build finished without producing Linux release assets in $output"
}

$assets = Get-ChildItem -LiteralPath $output -File |
    Where-Object {
        $_.Name -like "dlna-server-$Version-*" -or
        $_.Name -like "dlna-server_$($Version)_*" -or
        $_.Name -like "DLNA_Server-$Version-*"
    } |
    Sort-Object Name

$checksums = foreach ($asset in $assets) {
    "$(Get-Sha256Hex $asset.FullName)  $($asset.Name)"
}
$checksums | Set-Content -LiteralPath (Join-Path $output "SHA256SUMS.txt") -Encoding ASCII

Get-ChildItem -LiteralPath $output -File | Select-Object Name,Length,LastWriteTime
