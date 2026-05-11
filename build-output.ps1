param(
    [string]$Config = "Release",
    [string]$BuildDir = "build_output",
    [string]$OutputDir = "output",
    [switch]$KeepOutput
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildPath = Join-Path $repoRoot $BuildDir
$outputPath = Join-Path $repoRoot $OutputDir

function Assert-WorkspacePath {
    param([string]$PathToCheck)

    $fullPath = [System.IO.Path]::GetFullPath($PathToCheck)
    $fullRoot = [System.IO.Path]::GetFullPath($repoRoot)

    if (-not $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing path outside workspace: $fullPath"
    }

    return $fullPath
}

$buildPath = Assert-WorkspacePath $buildPath
$outputPath = Assert-WorkspacePath $outputPath

$isWindowsHost = $IsWindows -or $env:OS -eq "Windows_NT"

Write-Host "Configuring build in $buildPath"
$configureArgs = @("-S", $repoRoot, "-B", $buildPath, "-DCMAKE_INSTALL_PREFIX=$outputPath")
if (-not $isWindowsHost) {
    $configureArgs += "-DCMAKE_BUILD_TYPE=$Config"
}
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed."
}

Write-Host "Building $Config"
& cmake --build $buildPath --config $Config
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed."
}

if (-not (Test-Path $outputPath)) {
    New-Item -ItemType Directory -Path $outputPath | Out-Null
}

if (-not $KeepOutput) {
    Get-ChildItem -LiteralPath $outputPath -Force | Remove-Item -Recurse -Force
}

Write-Host "Installing artifacts to $outputPath"
& cmake --install $buildPath --config $Config --prefix $outputPath
if ($LASTEXITCODE -ne 0) {
    throw "CMake install failed."
}

if ($isWindowsHost) {
    $expected = Join-Path $outputPath "WinDLNAServer.exe"
} elseif ($IsMacOS) {
    $expected = Join-Path $outputPath "DLNA Server.app"
} else {
    $expected = Join-Path $outputPath "bin/dlna-server"
}

if (-not (Test-Path -LiteralPath $expected)) {
    throw "Expected output artifact missing: $expected"
}

Write-Host "Build output ready in $outputPath"
Get-ChildItem -LiteralPath $outputPath -Recurse -Force |
    Where-Object { -not $_.PSIsContainer } |
    Select-Object @{Name="Path"; Expression={ $_.FullName.Substring($outputPath.Length + 1) }}, Length, LastWriteTime |
    Format-Table -AutoSize
