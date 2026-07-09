param(
    [string]$Version = "",
    [string]$WslDistro = "Ubuntu",
    [string]$Platform = "winx64,winx86,linux,macos-x64,macos-arm64",
    [Alias("no-clean")]
    [switch]$NoClean
)

function Invoke-NativeChecked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "$FilePath failed with exit code $LASTEXITCODE"
        return $false
    }
    return $true
}

$ErrorActionPreference = "continue"
$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$output = Join-Path $repo "output"
$releaseTools = Join-Path $repo "build-release-tools"
$platformDirs = @{
    "winx64" = Join-Path $output "winx64"
    "winx86" = Join-Path $output "winx86"
    "linux" = Join-Path $output "linux"
    "macos-x64" = Join-Path $output "macos-x64"
    "macos-arm64" = Join-Path $output "macos-arm64"
}
$allPlatforms = @("winx64", "winx86", "linux", "macos-x64", "macos-arm64")

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

function Save-UrlIfMissing {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Url,
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [string]$Sha256 = ""
    )

    if (Test-Path -LiteralPath $Path) {
        $existing = Get-Item -LiteralPath $Path
        if ($existing.Length -gt 0) {
            if (-not $Sha256 -or ((Get-Sha256Hex -Path $Path) -ieq $Sha256)) {
                return
            }
            Remove-Item -LiteralPath $Path -Force
        }
    }

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
    Write-Host "Downloading $Url..."
    $client = New-Object System.Net.WebClient
    try {
        $client.DownloadFile($Url, $Path)
    } finally {
        $client.Dispose()
    }

    if ($Sha256 -and -not ((Get-Sha256Hex -Path $Path) -ieq $Sha256)) {
        Remove-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
        throw "Checksum mismatch for $Path"
    }
}

function Remove-DirectoryInsideRepo {
    param([Parameter(Mandatory = $true)][string]$Path)

    $resolvedRepo = Resolve-Path -LiteralPath $repo
    if (Test-Path -LiteralPath $Path) {
        $resolvedPath = Resolve-Path -LiteralPath $Path
        if (-not $resolvedPath.Path.StartsWith($resolvedRepo.Path, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove outside repo: $($resolvedPath.Path)"
        }
        Remove-Item -LiteralPath $resolvedPath.Path -Recurse -Force
    }
}

function Resolve-SelectedPlatforms {
    param([string]$Value)

    $selected = New-Object System.Collections.Generic.List[string]
    foreach ($raw in ($Value -split ",")) {
        $name = $raw.Trim().ToLowerInvariant()
        if (-not $name) { continue }
        if (-not $platformDirs.ContainsKey($name)) {
            throw "Unknown platform '$raw'. Use comma-separated values from: $($allPlatforms -join ', ')"
        }
        $selected.Add($name)
    }

    if ($selected.Count -eq 0) {
        throw "No platforms selected."
    }
    return @($selected | Select-Object -Unique)
}

function Initialize-PlatformOutput {
    param([Parameter(Mandatory = $true)][string]$Name)

    $dir = $platformDirs[$Name]
    if (-not $NoClean) {
        Remove-DirectoryInsideRepo -Path $dir
    }
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
}

function Test-SelectedPlatformPrerequisites {
    param([Parameter(Mandatory = $true)][string[]]$Names)

    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        throw "git not found. Source release archive requires git."
    }
    if (($Names -contains "winx64" -or $Names -contains "winx86") -and -not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        throw "cmake not found. Windows assets require CMake."
    }
    if (($Names -contains "linux") -and -not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
        throw "wsl.exe not found. Linux assets require WSL."
    }
    if (($Names -contains "linux") -and -not (Get-Command tar.exe -ErrorAction SilentlyContinue)) {
        throw "tar.exe not found. Linux release tool extraction requires tar."
    }
    if (($Names -contains "macos-x64" -or $Names -contains "macos-arm64") -and $env:OS -eq "Windows_NT") {
        Write-Warning "macOS assets must be built on macOS. Skipping macos platforms."
    }
}

function Resolve-VcpkgRoot {
    $candidateRoots = New-Object System.Collections.Generic.List[string]
    if ($env:VCPKG_ROOT) {
        $candidateRoots.Add($env:VCPKG_ROOT)
    }
    if ($env:USERPROFILE) {
        $candidateRoots.Add((Join-Path $env:USERPROFILE "vcpkg"))
    }

    foreach ($candidateRoot in ($candidateRoots | Select-Object -Unique)) {
        $toolchain = Join-Path $candidateRoot "scripts\buildsystems\vcpkg.cmake"
        if (Test-Path -LiteralPath $toolchain) {
            return $candidateRoot
        }
    }

    throw "vcpkg toolchain not found. Windows assets require VCPKG_ROOT or $env:USERPROFILE\vcpkg with curl installed."
}

function Get-VcpkgTripletForArch {
    param([Parameter(Mandatory = $true)][string]$Arch)

    switch ($Arch) {
        "x64" { return "x64-windows-static" }
        "Win32" { return "x86-windows-static" }
        default { throw "No vcpkg triplet configured for Windows architecture '$Arch'." }
    }
}

function New-SourceReleaseArchive {
    param(
        [Parameter(Mandatory = $true)][string]$ArchivePath
    )

    Invoke-NativeChecked "git" @("-C", $repo, "archive", "--format", "zip", "--output", $ArchivePath, "HEAD")
}

function Invoke-CmakeBuild {
    param(
        [Parameter(Mandatory = $true)][string]$BuildDir,
        [Parameter(Mandatory = $true)][string]$Arch,
        [Parameter(Mandatory = $true)][string]$InstallDir
    )

    $fullBuildDir = Join-Path $repo $BuildDir
    $vcpkgRoot = Resolve-VcpkgRoot
    $vcpkgToolchain = Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"
    $vcpkgTriplet = Get-VcpkgTripletForArch -Arch $Arch
    $curlConfig = Join-Path $vcpkgRoot "installed\$vcpkgTriplet\share\curl\CURLConfig.cmake"
    if (-not (Test-Path -LiteralPath $curlConfig)) {
        throw "curl:$vcpkgTriplet not installed in vcpkg. Run: vcpkg install curl:$vcpkgTriplet"
    }

    Remove-DirectoryInsideRepo -Path $fullBuildDir
    $cmakeResult = Invoke-NativeChecked "cmake" @(
        "-S", $repo,
        "-B", $fullBuildDir,
        "-A", $Arch,
        "-DCMAKE_INSTALL_PREFIX=$InstallDir",
        "-DCMAKE_TOOLCHAIN_FILE=$vcpkgToolchain",
        "-DVCPKG_TARGET_TRIPLET=$vcpkgTriplet"
    )
    if (-not $cmakeResult) { return $false }
    $buildResult = Invoke-NativeChecked "cmake" @("--build", $fullBuildDir, "--config", "Release", "--target", "install", "--", "/m")
    if (-not $buildResult) { return $false }
    return $true
}

if (-not $Version) {
    $cmake = Get-Content -LiteralPath (Join-Path $repo "CMakeLists.txt") -Raw
    if ($cmake -notmatch "project\(dlna-server VERSION ([0-9.]+)\)") {
        throw "Could not read project version from CMakeLists.txt"
    }
    $Version = $Matches[1]
}

$selectedPlatforms = Resolve-SelectedPlatforms -Value $Platform
Test-SelectedPlatformPrerequisites -Names $selectedPlatforms
New-Item -ItemType Directory -Force -Path $output | Out-Null
foreach ($selectedPlatform in $selectedPlatforms) {
    Initialize-PlatformOutput -Name $selectedPlatform
}
$sourceZip = Join-Path $output "dlna-server-$Version-source.zip"
New-SourceReleaseArchive -ArchivePath $sourceZip

if ($selectedPlatforms -contains "winx64") {
    if (Invoke-CmakeBuild -BuildDir "build-release-winx64" -Arch "x64" -InstallDir $platformDirs["winx64"]) {
        Compress-Archive -LiteralPath (Join-Path $platformDirs["winx64"] "DLNA Server.exe") -DestinationPath (Join-Path $platformDirs["winx64"] "dlna-server-$Version-windows-x86_64.zip") -Force
    }
}

if ($selectedPlatforms -contains "winx86") {
    if (Invoke-CmakeBuild -BuildDir "build-release-winx86" -Arch "Win32" -InstallDir $platformDirs["winx86"]) {
        Compress-Archive -LiteralPath (Join-Path $platformDirs["winx86"] "DLNA Server.exe") -DestinationPath (Join-Path $platformDirs["winx86"] "dlna-server-$Version-windows-x86.zip") -Force
    }
}

$drive = $repo.Substring(0, 1).ToLowerInvariant()
$repoWsl = "/mnt/$drive" + $repo.Substring(2).Replace("\", "/")
$repoWslEscaped = $repoWsl.Replace("'", "'\''")
$toolsDir = Join-Path $releaseTools "windows"
$fltkTag = "release-1.4.5"
$fltkSha256 = "7715e69ce081fa9ce6da48bb0dd3b07a4cf2cf937813814c04272f36fff593ea"
$fltkSource = Join-Path $toolsDir "fltk-$fltkTag"
$fltkArchive = Join-Path $toolsDir "fltk-$fltkTag.tar.gz"

if ($selectedPlatforms -contains "linux") {
    if (-not (Test-Path -LiteralPath (Join-Path $fltkSource "CMakeLists.txt"))) {
        New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null
        $fltkUrl = "https://github.com/fltk/fltk/archive/refs/tags/$fltkTag.tar.gz"
        Save-UrlIfMissing $fltkUrl $fltkArchive $fltkSha256

        $extractDir = Join-Path $toolsDir "fltk-extract"
        Remove-DirectoryInsideRepo -Path $extractDir
        New-Item -ItemType Directory -Force -Path $extractDir | Out-Null
        Invoke-NativeChecked "tar.exe" @("-xzf", $fltkArchive, "-C", $extractDir)

        $extractedSource = Get-ChildItem -LiteralPath $extractDir -Directory | Select-Object -First 1
        if (-not $extractedSource) {
            throw "Could not extract FLTK archive: $fltkArchive"
        }
        Remove-DirectoryInsideRepo -Path $fltkSource
        Move-Item -LiteralPath $extractedSource.FullName -Destination $fltkSource
        Remove-DirectoryInsideRepo -Path $extractDir
    }

    $linuxdeployVersion = "1-alpha-20251107-1"
    $linuxdeployPath = Join-Path $toolsDir "linuxdeploy-x86_64.AppImage"
    $runtimePath = Join-Path $toolsDir "runtime-x86_64"
    Save-UrlIfMissing "https://github.com/linuxdeploy/linuxdeploy/releases/download/$linuxdeployVersion/linuxdeploy-x86_64.AppImage" $linuxdeployPath "c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"
    Save-UrlIfMissing "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64" $runtimePath "a2419dce47568395ae79c01ffa9a5a341dd339581352ff104d073527543177e5"

    $fltkWsl = "/mnt/$drive" + $fltkSource.Substring(2).Replace("\", "/")
    $linuxdeployWsl = "/mnt/$drive" + $linuxdeployPath.Substring(2).Replace("\", "/")
    $runtimeWsl = "/mnt/$drive" + $runtimePath.Substring(2).Replace("\", "/")
    $linuxOutputWsl = "/mnt/$drive" + $platformDirs["linux"].Substring(2).Replace("\", "/")
    $linuxStageWsl = "$repoWsl/build-release-linux-stage"
    $releaseToolsWsl = "/mnt/$drive" + (Join-Path $releaseTools "linux").Substring(2).Replace("\", "/")
    $noCleanValue = if ($NoClean) { "1" } else { "0" }
    $bashTemplate = @'
cd '__REPO_WSL__' &&
tr -d '\r' < scripts/build-linux-desktop-assets.sh > /tmp/dlna-server-build-linux-desktop-assets.sh &&
chmod +x /tmp/dlna-server-build-linux-desktop-assets.sh &&
DLNA_REPO_ROOT='__REPO_WSL__' DLNA_OUTPUT_DIR='__LINUX_OUTPUT_WSL__' DLNA_LINUX_PLATFORM_DIR='__LINUX_OUTPUT_WSL__' DLNA_LINUX_STAGE_DIR='__LINUX_STAGE_WSL__' DLNA_RELEASE_TOOLS_DIR='__TOOLS_WSL__' DLNA_NO_CLEAN='__NO_CLEAN__' DLNA_FLTK_SOURCE_DIR='__FLTK_WSL__' LINUXDEPLOY='__LINUXDEPLOY_WSL__' APPIMAGE_RUNTIME='__RUNTIME_WSL__' DLNA_SERVER_VERSION='__VERSION__' bash /tmp/dlna-server-build-linux-desktop-assets.sh
'@
    $bashCommand = $bashTemplate.Replace("__REPO_WSL__", $repoWslEscaped)
    $bashCommand = $bashCommand.Replace("__LINUX_OUTPUT_WSL__", $linuxOutputWsl.Replace("'", "'\''"))
    $bashCommand = $bashCommand.Replace("__LINUX_STAGE_WSL__", $linuxStageWsl.Replace("'", "'\''"))
    $bashCommand = $bashCommand.Replace("__TOOLS_WSL__", $releaseToolsWsl.Replace("'", "'\''"))
    $bashCommand = $bashCommand.Replace("__NO_CLEAN__", $noCleanValue)
    $bashCommand = $bashCommand.Replace("__FLTK_WSL__", $fltkWsl.Replace("'", "'\''"))
    $bashCommand = $bashCommand.Replace("__LINUXDEPLOY_WSL__", $linuxdeployWsl.Replace("'", "'\''"))
    $bashCommand = $bashCommand.Replace("__RUNTIME_WSL__", $runtimeWsl.Replace("'", "'\''"))
    $bashCommand = $bashCommand.Replace("__VERSION__", $Version) -replace "`r?`n", " "
    Invoke-NativeChecked "wsl.exe" @("-d", $WslDistro, "--", "bash", "-lc", $bashCommand)

    $linuxAssets = Get-ChildItem -LiteralPath $platformDirs["linux"] -File |
        Where-Object {
            $_.Name -like "dlna-server_$($Version)_*.deb" -or
            $_.Name -like "dlna-server-$Version-linux-*" -or
            $_.Name -like "DLNA_Server-$Version-*.AppImage"
        }
    if (-not $linuxAssets) {
        Write-Warning "Linux build finished without producing Linux release assets in $($platformDirs["linux"])"
    }
}

foreach ($macPlatform in @("macos-x64", "macos-arm64")) {
    if ($selectedPlatforms -contains $macPlatform) {
        if ($env:OS -eq "Windows_NT") {
            Write-Warning "$macPlatform assets must be built on macOS. Skipping."
            continue
        }
        $arch = if ($macPlatform -eq "macos-x64") { "x86_64" } else { "arm64" }
        $env:DLNA_MACOS_ARCH = $arch
        $env:DLNA_MACOS_PLATFORM_DIR = $platformDirs[$macPlatform]
        $env:DLNA_NO_CLEAN = if ($NoClean) { "1" } else { "0" }
        try {
            $result = Invoke-NativeChecked "bash" @("scripts/build-macos-dmg.sh")
            if (-not $result) {
                Write-Warning "macOS build failed for $macPlatform"
            }
        } finally {
            Remove-Item Env:\DLNA_MACOS_ARCH -ErrorAction SilentlyContinue
            Remove-Item Env:\DLNA_MACOS_PLATFORM_DIR -ErrorAction SilentlyContinue
            Remove-Item Env:\DLNA_NO_CLEAN -ErrorAction SilentlyContinue
        }
    }
}

$platformAssets = foreach ($selectedPlatform in $selectedPlatforms) {
    Get-ChildItem -LiteralPath $platformDirs[$selectedPlatform] -File -ErrorAction SilentlyContinue |
        Where-Object {
            $_.Name -like "dlna-server-$Version-*" -or
            $_.Name -like "dlna-server_$($Version)_*" -or
            $_.Name -like "DLNA_Server-$Version-*"
        }
}
if (-not (Test-Path -LiteralPath $sourceZip) -and -not $platformAssets) {
    Write-Warning "No release assets produced."
}

foreach ($selectedPlatform in $selectedPlatforms) {
    Get-ChildItem -LiteralPath $platformDirs[$selectedPlatform] -File -ErrorAction SilentlyContinue |
        Select-Object @{Name = "Platform"; Expression = { $selectedPlatform } }, Name, Length, LastWriteTime
}
