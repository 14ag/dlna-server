param(
    [string]$InstallDir = "output",
    [string]$AppDir = "output/DLNA_Server.AppDir"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path

function Resolve-WorkspacePath {
    param([string]$PathToCheck)

    $fullPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $PathToCheck))
    $fullRoot = [System.IO.Path]::GetFullPath($repoRoot).TrimEnd('\', '/')

    if (-not $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing path outside workspace: $fullPath"
    }

    return $fullPath
}

function Copy-RequiredItem {
    param(
        [string]$Source,
        [string]$Destination
    )

    if (-not (Test-Path -LiteralPath $Source)) {
        throw "Required AppDir input missing: $Source"
    }

    $parent = Split-Path -Parent $Destination
    if (-not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent | Out-Null
    }

    Copy-Item -LiteralPath $Source -Destination $Destination -Recurse -Force
}

$installPath = Resolve-WorkspacePath $InstallDir
$appDirPath = Resolve-WorkspacePath $AppDir

if (-not (Test-Path -LiteralPath $installPath)) {
    throw "Install tree missing: $installPath. Run build-output.ps1 on Linux first."
}

if (Test-Path -LiteralPath $appDirPath) {
    Remove-Item -LiteralPath $appDirPath -Recurse -Force
}

New-Item -ItemType Directory -Path $appDirPath | Out-Null

Copy-RequiredItem (Join-Path $installPath "bin/dlna-server") (Join-Path $appDirPath "usr/bin/dlna-server")
Copy-RequiredItem (Join-Path $installPath "bin/dlna-server-gui") (Join-Path $appDirPath "usr/bin/dlna-server-gui")
Copy-RequiredItem (Join-Path $installPath "share/dlna-server") (Join-Path $appDirPath "usr/share/dlna-server")
Copy-RequiredItem (Join-Path $installPath "share/icons/hicolor/scalable/apps/dlna-server.svg") (Join-Path $appDirPath "usr/share/icons/hicolor/scalable/apps/dlna-server.svg")
Copy-RequiredItem (Join-Path $repoRoot "packaging/linux/AppRun") (Join-Path $appDirPath "AppRun")
Copy-RequiredItem (Join-Path $repoRoot "resources/dlna-server.svg") (Join-Path $appDirPath "dlna-server.svg")

$desktopText = @"
[Desktop Entry]
Type=Application
Name=DLNA Server
Comment=Share local media with DLNA and UPnP clients
Exec=dlna-server-gui
Icon=dlna-server
Terminal=false
Categories=AudioVideo;Network;
StartupNotify=true
"@

Set-Content -LiteralPath (Join-Path $appDirPath "dlna-server.desktop") -Value $desktopText -Encoding utf8NoBOM

if ($IsLinux -or $IsMacOS) {
    chmod +x (Join-Path $appDirPath "AppRun")
    chmod +x (Join-Path $appDirPath "usr/bin/dlna-server")
    chmod +x (Join-Path $appDirPath "usr/bin/dlna-server-gui")
}

$required = @(
    "AppRun",
    "dlna-server.desktop",
    "dlna-server.svg",
    "usr/bin/dlna-server",
    "usr/bin/dlna-server-gui",
    "usr/share/dlna-server/posix_gui.py",
    "usr/share/icons/hicolor/scalable/apps/dlna-server.svg"
)

foreach ($relative in $required) {
    $path = Join-Path $appDirPath $relative
    if (-not (Test-Path -LiteralPath $path)) {
        throw "AppDir validation failed, missing: $relative"
    }
}

Write-Host "AppDir ready: $appDirPath"
Get-ChildItem -LiteralPath $appDirPath -Recurse -Force |
    Where-Object { -not $_.PSIsContainer } |
    Select-Object @{Name="Path"; Expression={ $_.FullName.Substring($appDirPath.Length + 1) }}, Length |
    Format-Table -AutoSize
