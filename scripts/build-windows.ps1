<#
.SYNOPSIS
    Build DLNA Server Windows binaries for selected architectures.
.DESCRIPTION
    Runs CMake configure + build + install using vcpkg for curl.
    Stores outputs in output/winx64 and output/winx86.
.PARAMETER Arch
    Archs to build: 'x64', 'Win32', or 'both' (default: 'both').
#>
param(
    [string]$Arch = "both"
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$OutputDir = Join-Path $RepoRoot "output"

function Invoke-NativeChecked {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$FilePath failed with exit code $LASTEXITCODE" }
    return $true
}

function Remove-DirectoryInsideRepo {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$RepoRoot
    )
    $resolvedRepo = Resolve-Path -LiteralPath $RepoRoot
    if (Test-Path -LiteralPath $Path) {
        $resolved = Resolve-Path -LiteralPath $Path
        if (-not $resolved.Path.StartsWith($resolvedRepo.Path, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove path outside repo: $($resolved.Path)"
        }
        Remove-Item -LiteralPath $resolved.Path -Recurse -Force
    }
}

function Resolve-VcpkgRoot {
    $candidates = @()
    if ($env:VCPKG_ROOT)              { $candidates += $env:VCPKG_ROOT }
    if ($env:VCPKG_INSTALLATION_ROOT) { $candidates += $env:VCPKG_INSTALLATION_ROOT }
    if ($env:USERPROFILE)             { $candidates += (Join-Path $env:USERPROFILE "vcpkg") }
    foreach ($c in ($candidates | Select-Object -Unique)) {
        if (Test-Path -LiteralPath (Join-Path $c "scripts\buildsystems\vcpkg.cmake")) {
            return $c
        }
    }
    throw 'vcpkg not found. Set VCPKG_ROOT, VCPKG_INSTALLATION_ROOT, or install vcpkg at $env:USERPROFILE\vcpkg.'
}

function Build-Arch {
    param([string]$Architecture)

    $suffix = if ($Architecture -eq "Win32") { "winx86" } else { "winx64" }
    $InstallDir = Join-Path $OutputDir $suffix
    $BuildDir = Join-Path $RepoRoot "build-release-$suffix"
    $triplet = if ($Architecture -eq "Win32") { "x86-windows-static" } else { "x64-windows-static" }

    $vcpkgRoot = Resolve-VcpkgRoot
    $toolchain = Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"
    $curlConfig = Join-Path $vcpkgRoot "installed\$triplet\share\curl\CURLConfig.cmake"

    if (-not (Test-Path -LiteralPath $curlConfig)) {
        Write-Host "curl:$triplet not found in vcpkg - installing..."
        $vcpkgExe = Join-Path $vcpkgRoot "vcpkg.exe"
        Invoke-NativeChecked $vcpkgExe @("install", "curl:$triplet")
    }

    Remove-DirectoryInsideRepo -Path $BuildDir -RepoRoot $RepoRoot
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null

    Invoke-NativeChecked "cmake" @(
        "-S", $RepoRoot,
        "-B", $BuildDir,
        "-A", $Architecture,
        "-DCMAKE_INSTALL_PREFIX=$InstallDir",
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
        "-DVCPKG_TARGET_TRIPLET=$triplet"
    )
    Invoke-NativeChecked "cmake" @(
        "--build", $BuildDir,
        "--config", "Release",
        "--target", "install",
        "--", "/m"
    )

    $cmake = Get-Content -LiteralPath (Join-Path $RepoRoot "CMakeLists.txt") -Raw
    if ($cmake -match 'project\(dlna-server\s+VERSION\s+([0-9.]+)\)') {
        $version = $Matches[1]
    } else {
        throw "Could not read version"
    }

    Compress-Archive -LiteralPath (Join-Path $InstallDir "DLNA Server.exe") -DestinationPath (Join-Path $InstallDir "dlna-server-$version-windows-$Architecture.zip") -Force

    Write-Host "Windows $Architecture build completed and zipped in: $InstallDir"
}

$builds = @()
if ($Arch -eq "both") {
    $builds += "x64"
    $builds += "Win32"
} else {
    $builds += $Arch
}

foreach ($b in $builds) {
    Build-Arch -Architecture $b
}
