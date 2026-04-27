param(
    [string]$Config = "Release",
    [string]$BuildDir = "build_output",
    [string]$OutputDir = "output"
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

Write-Host "Configuring build in $buildPath"
cmake -S $repoRoot -B $buildPath | Write-Host
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed."
}

Write-Host "Building $Config"
cmake --build $buildPath --config $Config | Write-Host
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed."
}

$configOutputPath = Join-Path $buildPath $Config
if (-not (Test-Path $configOutputPath)) {
    throw "Expected build output folder missing: $configOutputPath"
}

if (-not (Test-Path $outputPath)) {
    New-Item -ItemType Directory -Path $outputPath | Out-Null
}

Get-ChildItem -LiteralPath $outputPath -Force | Remove-Item -Recurse -Force

$builtItems = Get-ChildItem -LiteralPath $configOutputPath -File | Where-Object {
    $_.BaseName -eq "WinDLNAServer" -or $_.Name -like "WinDLNAServer.*"
}

if (-not $builtItems) {
    throw "No built WinDLNAServer items found in $configOutputPath"
}

foreach ($item in $builtItems) {
    Move-Item -LiteralPath $item.FullName -Destination (Join-Path $outputPath $item.Name) -Force
}

Write-Host "Moved build output to $outputPath"
Get-ChildItem -LiteralPath $outputPath | Select-Object Name, Length, LastWriteTime | Format-Table -AutoSize
