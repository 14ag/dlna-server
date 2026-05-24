param(
    [string]$Version = "",
    [string]$WslDistro = "Ubuntu"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$output = Join-Path $repo "output"

if (-not $Version) {
    $cmake = Get-Content -LiteralPath (Join-Path $repo "CMakeLists.txt") -Raw
    if ($cmake -notmatch "project\(dlna-server VERSION ([0-9.]+)\)") {
        throw "Could not read project version from CMakeLists.txt"
    }
    $Version = $Matches[1]
}

New-Item -ItemType Directory -Force -Path $output | Out-Null
Get-ChildItem -LiteralPath $output -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like "dlna-server-$Version-*" -or $_.Name -like "DLNA_Server-$Version-*" -or $_.Name -eq "SHA256SUMS.txt" } |
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

    cmake -S $repo -B $fullBuildDir -A $Arch "-DCMAKE_INSTALL_PREFIX=$fullInstallDir"
    cmake --build $fullBuildDir --config Release --target install -- /m
}

Invoke-CmakeBuild -BuildDir "build-release-windows" -Arch "x64" -InstallDir "output/windows"
Invoke-CmakeBuild -BuildDir "build-release-windows-x86" -Arch "Win32" -InstallDir "output/windows-x86"

Compress-Archive -LiteralPath (Join-Path $output "windows/DLNA Server.exe") -DestinationPath (Join-Path $output "dlna-server-$Version-windows-x86_64.zip") -Force
Compress-Archive -LiteralPath (Join-Path $output "windows-x86/DLNA Server.exe") -DestinationPath (Join-Path $output "dlna-server-$Version-windows-x86.zip") -Force

$drive = $repo.Substring(0, 1).ToLowerInvariant()
$repoWsl = "/mnt/$drive" + $repo.Substring(2).Replace("\", "/")
& wsl.exe -d $WslDistro -- bash -lc "cd '$repoWsl' && bash scripts/build-linux-desktop-assets.sh"

$assets = Get-ChildItem -LiteralPath $output -File |
    Where-Object { $_.Name -like "dlna-server-$Version-*" -or $_.Name -like "DLNA_Server-$Version-*" } |
    Sort-Object Name

$checksums = foreach ($asset in $assets) {
    $hash = Get-FileHash -Algorithm SHA256 -LiteralPath $asset.FullName
    "$($hash.Hash.ToLower())  $($asset.Name)"
}
$checksums | Set-Content -LiteralPath (Join-Path $output "SHA256SUMS.txt") -Encoding ASCII

Get-ChildItem -LiteralPath $output -File | Select-Object Name,Length,LastWriteTime
