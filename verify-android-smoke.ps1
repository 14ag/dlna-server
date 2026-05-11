param(
    [switch]$KeepFirewallRules
)

$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent $MyInvocation.MyCommand.Path
$exePath = Join-Path $repo "output\WinDLNAServer.exe"
$outDir = Join-Path $repo "output"
$appDataDir = Join-Path $env:APPDATA "WinDLNAServer"
$configPath = Join-Path $outDir "config.ini"
$debugLogPath = Join-Path $appDataDir "debug.log"
$resultsPath = Join-Path $outDir "android-verification-results.txt"
$debugCopyPath = Join-Path $outDir "android-verification-debug.log"
$testMediaDir = Join-Path $env:TEMP "WinDLNAServer-Android-TestMedia"
$serverProc = $null
$backupPath = $null
$summary = New-Object System.Collections.Generic.List[string]

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Kernel32IniBridge {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool WritePrivateProfileString(string appName, string keyName, string value, string fileName);
}
"@

function Add-Result([string]$line) {
    $summary.Add($line) | Out-Null
    Write-Host $line
}

function Set-IniValue([string]$section, [string]$key, [string]$value) {
    if (-not [Kernel32IniBridge]::WritePrivateProfileString($section, $key, $value, $configPath)) {
        throw "Failed to write INI value $section/$key"
    }
}

function Invoke-AdbText([string[]]$adbArgs) {
    $output = & adb @adbArgs 2>&1
    if ($LASTEXITCODE -ne 0) {
        $text = (($output | Out-String).Trim())
        if (-not $text) {
            $text = "adb exited with code $LASTEXITCODE"
        }
        throw $text
    }
    return (($output | Out-String).Trim())
}

function Get-AndroidIPv4 {
    $ipOutput = Invoke-AdbText @("shell", "ip -4 addr show wlan0")
    $match = [regex]::Match($ipOutput, "inet\s+(\d+\.\d+\.\d+\.\d+)/")
    if (-not $match.Success) {
        throw "Android wlan0 IPv4 not found"
    }
    return $match.Groups[1].Value
}

function Get-PCAddressForAndroid([string]$androidIp) {
    $octets = $androidIp.Split(".")
    $prefix = "$($octets[0]).$($octets[1]).$($octets[2])."
    $candidate = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object { $_.IPAddress -like "$prefix*" -and $_.IPAddress -ne $androidIp } |
        Select-Object -First 1
    if (-not $candidate) {
        throw "No Windows IPv4 address found on Android subnet $prefix*"
    }
    return $candidate.IPAddress
}

try {
    powershell Get-Command adb -ErrorAction SilentlyContinue | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "adb missing"
    }
    if (-not (Test-Path $exePath)) {
        throw "Missing built exe at $exePath"
    }

    $devices = Invoke-AdbText @("devices", "-l")
    if ($devices -notmatch "\bdevice\b") {
        throw "No authorized Android device visible to adb"
    }
    if ((Invoke-AdbText @("shell", "pm list packages org.videolan.vlc")) -notmatch "org.videolan.vlc") {
        throw "Android VLC package org.videolan.vlc not installed"
    }
    Add-Result "PASS adb device and Android VLC detected"

    $androidIp = Get-AndroidIPv4
    $pcIp = Get-PCAddressForAndroid $androidIp
    Add-Result "PASS Android wlan0=$androidIp Windows peer=$pcIp"
    try {
        Get-NetFirewallRule -DisplayName "WinDLNAServer Android Smoke TCP 18200" -ErrorAction SilentlyContinue | Remove-NetFirewallRule
        Get-NetFirewallRule -DisplayName "WinDLNAServer Android Smoke UDP 1900" -ErrorAction SilentlyContinue | Remove-NetFirewallRule
        New-NetFirewallRule -DisplayName "WinDLNAServer Android Smoke TCP 18200" -Direction Inbound -Action Allow -Protocol TCP -LocalPort 18200 -Profile Any -ErrorAction SilentlyContinue | Out-Null
        New-NetFirewallRule -DisplayName "WinDLNAServer Android Smoke UDP 1900" -Direction Inbound -Action Allow -Protocol UDP -LocalPort 1900 -Profile Any -ErrorAction SilentlyContinue | Out-Null
        Add-Result "PASS firewall allowances present for TCP 18200 and UDP 1900"
    } catch {
        Add-Result ("WARN firewall rule setup failed: " + $_.Exception.Message)
    }

    New-Item -ItemType Directory -Path $appDataDir -Force | Out-Null
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
    if (Test-Path $configPath) {
        $backupPath = Join-Path $env:TEMP ("WinDLNAServer-config-backup-" + [guid]::NewGuid().ToString() + ".ini")
        Copy-Item -LiteralPath $configPath -Destination $backupPath -Force
    }
    if (Test-Path $debugLogPath) {
        Remove-Item -LiteralPath $debugLogPath -Force
    }
    if (Test-Path $testMediaDir) {
        Remove-Item -LiteralPath $testMediaDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $testMediaDir | Out-Null
    Set-Content -LiteralPath (Join-Path $testMediaDir "android-smoke.mp3") -Value "fake mp3 bytes" -Encoding ascii

    if (Test-Path $configPath) {
        Remove-Item -LiteralPath $configPath -Force
    }
    Set-IniValue "Settings" "ServerName" "WinDLNA Android Smoke"
    Set-IniValue "Settings" "Port" "18200"
    Set-IniValue "Settings" "FileServerPort" "18201"
    Set-IniValue "Settings" "FlatFolderStyle" "0"
    Set-IniValue "Settings" "ShowFileNamesInsteadOfTitles" "0"
    Set-IniValue "Settings" "ProxyStreams" "0"
    Set-IniValue "Settings" "SortByTitle" "0"
    Set-IniValue "Settings" "DoNotShowAllMediaFolders" "0"
    Set-IniValue "Settings" "AddArtistAlbumFolders" "0"
    Set-IniValue "Settings" "DebugLog" "1"
    Set-IniValue "Settings" "RunOnBoot" "0"
    Set-IniValue "Settings" "IPWhiteList" ""
    Set-IniValue "Settings" "DeviceUUID" "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"
    Set-IniValue "Settings" "MediaSources" $testMediaDir

    $serverProc = Start-Process -FilePath $exePath -ArgumentList "--minimized" -WindowStyle Hidden -PassThru
    $listenReady = $false
    for ($i = 0; $i -lt 40; $i++) {
        Start-Sleep -Milliseconds 500
        $listen = Get-NetTCPConnection -State Listen -LocalPort 18200 -ErrorAction SilentlyContinue
        if ($listen) {
            $listenReady = $true
            break
        }
    }
    if (-not $listenReady) {
        throw "Server did not listen on TCP 18200"
    }
    Add-Result "PASS server listening on TCP 18200"

    $desc = Invoke-AdbText @("shell", "curl -sS -m 8 http://$pcIp`:18200/description.xml 2>&1")
    if ($desc -match "WinDLNA Android Smoke" -and $desc -match "ContentDirectory:1") {
        Add-Result "PASS Android curl fetched description.xml from Windows server"
    } else {
        throw "Android curl description.xml missing expected DLNA XML"
    }

    $preLog = if (Test-Path $debugLogPath) { Get-Content -LiteralPath $debugLogPath -Raw } else { "" }
    try {
        Invoke-AdbText @("shell", "am force-stop org.videolan.vlc") | Out-Null
    } catch {
        Add-Result ("WARN VLC force-stop failed: " + $_.Exception.Message)
    }
    try {
        Invoke-AdbText @("shell", "am start -a android.intent.action.VIEW -d upnp:// -n org.videolan.vlc/.gui.video.VideoPlayerActivity") | Out-Null
    } catch {
        if ($_.Exception.Message -match "Activity not started") {
            Add-Result "WARN Android reused existing VLC task"
        } else {
            throw
        }
    }
    Start-Sleep -Seconds 12
    $postLog = if (Test-Path $debugLogPath) { Get-Content -LiteralPath $debugLogPath -Raw } else { "" }
    $newLog = $postLog.Substring([Math]::Min($preLog.Length, $postLog.Length))
    if ($newLog -match "SSDP search in: src=$([regex]::Escape($androidIp))") {
        Add-Result "PASS Android VLC triggered SSDP M-SEARCH seen from $androidIp"
    } else {
        Add-Result "WARN Android VLC launch did not produce SSDP search in server log"
    }

    if ($postLog -match "SSDP response sent: dst=$([regex]::Escape($androidIp))") {
        Add-Result "PASS server sent SSDP response to Android device"
    } else {
        Add-Result "WARN no SSDP response to Android found in server log"
    }

    if (Test-Path $debugLogPath) {
        Copy-Item -LiteralPath $debugLogPath -Destination $debugCopyPath -Force
    }
    Set-Content -LiteralPath $resultsPath -Value ($summary -join [Environment]::NewLine) -Encoding UTF8
} finally {
    if ($serverProc) {
        try {
            $serverProc.Refresh()
            if (-not $serverProc.HasExited) {
                Stop-Process -Id $serverProc.Id -Force -ErrorAction SilentlyContinue
            }
        } catch {
        }
    }
    if ($backupPath -and (Test-Path $backupPath)) {
        Copy-Item -LiteralPath $backupPath -Destination $configPath -Force
        Remove-Item -LiteralPath $backupPath -Force
    } elseif (Test-Path $configPath) {
        Remove-Item -LiteralPath $configPath -Force
    }
    if (-not $KeepFirewallRules) {
        Get-NetFirewallRule -DisplayName "WinDLNAServer Android Smoke TCP 18200" -ErrorAction SilentlyContinue | Remove-NetFirewallRule
        Get-NetFirewallRule -DisplayName "WinDLNAServer Android Smoke UDP 1900" -ErrorAction SilentlyContinue | Remove-NetFirewallRule
    }
}
