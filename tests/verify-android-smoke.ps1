param(
    [switch]$KeepFirewallRules,
    [string]$DeviceSerial = "DKJ9X18709W05461"
)

$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$exePath = Join-Path $repo "output\dlna-server.exe"
$outDir = Join-Path $repo "output"
$appDataDir = Join-Path $env:APPDATA "dlna-server"
$configPath = Join-Path $outDir "config.ini"
$debugLogPath = Join-Path $appDataDir "debug.log"
$resultsPath = Join-Path $outDir "android-verification-results.txt"
$debugCopyPath = Join-Path $outDir "android-verification-debug.log"
$testMediaDir = Join-Path $env:TEMP "dlna-server-Android-TestMedia"
$serverProc = $null
$backupPath = $null
$summary = New-Object System.Collections.Generic.List[string]
$serverPort = 18200
$ssdpPort = 1900
$firewallReadDenied = $false
$adbTargetArgs = @()

if (-not ("Kernel32IniBridge" -as [type])) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Kernel32IniBridge {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool WritePrivateProfileString(string appName, string keyName, string value, string fileName);
}
"@
}

function Add-Result([string]$line) {
    $summary.Add($line) | Out-Null
    Write-Host $line
}

function Read-DebugLog {
    if (-not (Test-Path $debugLogPath)) {
        return ""
    }

    for ($i = 0; $i -lt 10; $i++) {
        try {
            return Get-Content -LiteralPath $debugLogPath -Raw -ErrorAction Stop
        } catch {
            Start-Sleep -Milliseconds 150
        }
    }

    return ""
}

function Assert-Command([string]$tool) {
    $found = powershell Get-Command $tool -ErrorAction SilentlyContinue
    if (-not $found) {
        throw "$tool missing"
    }
}

function Invoke-AdbHostText([string[]]$adbArgs) {
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

function Select-AdbDevice {
    $devices = Invoke-AdbHostText @("devices", "-l")
    $deviceLines = @($devices -split "`r?`n" | Where-Object { $_ -match "\bdevice\b" -and $_ -notmatch "^List of devices" })
    if ($deviceLines.Count -eq 0) {
        throw "No authorized Android device visible to adb"
    }

    if ($DeviceSerial) {
        $match = $deviceLines | Where-Object { $_ -match "^$([regex]::Escape($DeviceSerial))\s+" } | Select-Object -First 1
        if (-not $match) {
            throw "ADB device $DeviceSerial not found or not authorized. adb devices: $devices"
        }
        return $DeviceSerial
    }

    if ($deviceLines.Count -eq 1) {
        return (($deviceLines[0] -split "\s+")[0])
    }

    throw "More than one ADB device found. Rerun with -DeviceSerial <serial>. adb devices: $devices"
}

function Test-IsAdmin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Set-IniValue([string]$section, [string]$key, [string]$value) {
    if (-not [Kernel32IniBridge]::WritePrivateProfileString($section, $key, $value, $configPath)) {
        throw "Failed to write INI value $section/$key"
    }
}

function Invoke-AdbText([string[]]$adbArgs) {
    $output = & adb @script:adbTargetArgs @adbArgs 2>&1
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
    $ipOutput = Invoke-AdbText @("shell", "ip", "-4", "addr", "show", "wlan0")
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

function Assert-PhoneUnlocked {
    $windowState = Invoke-AdbText @("shell", "dumpsys", "window")
    if ($windowState -match "mShowingLockscreen=true|mDreamingLockscreen=true|isStatusBarKeyguard=true|mKeyguardShowing=true") {
        throw "Phone is locked; unlock it and rerun Android VLC discovery smoke"
    }
}

function Test-DlnaFirewallRule([string]$name, [string]$program, [string]$protocol, [int]$port) {
    $script:firewallReadDenied = $false
    try {
        $rule = Get-NetFirewallRule -DisplayName $name -ErrorAction Stop |
            Where-Object { $_.Direction -eq "Inbound" -and $_.Action -eq "Allow" -and $_.Enabled -eq "True" } |
            Select-Object -First 1
    } catch {
        if ($_.Exception.Message -match "Access is denied") {
            $script:firewallReadDenied = $true
            return $null
        }
        return $false
    }
    if (-not $rule) {
        return $false
    }

    $app = $rule | Get-NetFirewallApplicationFilter -ErrorAction SilentlyContinue
    $ports = $rule | Get-NetFirewallPortFilter -ErrorAction SilentlyContinue
    $addr = $rule | Get-NetFirewallAddressFilter -ErrorAction SilentlyContinue
    if (-not $app -or -not $ports -or -not $addr) {
        return $false
    }

    $localPort = [string]$ports.LocalPort
    $portOk = if ($port -eq 0) {
        [string]::IsNullOrWhiteSpace($localPort) -or $localPort -eq "Any" -or $localPort -eq "*"
    } else {
        $localPort -eq [string]$port
    }

    return (($app.Program -ieq $program) -and
            ($ports.Protocol -ieq $protocol) -and
            $portOk -and
            ([string]$addr.RemoteAddress -eq "LocalSubnet"))
}

function Ensure-FirewallAccess {
    $tcpOk = Test-DlnaFirewallRule "dlna-server HTTP TCP" $exePath "TCP" 0
    $tcpReadDenied = $firewallReadDenied
    $udpOk = Test-DlnaFirewallRule "dlna-server SSDP UDP" $exePath "UDP" $ssdpPort
    $udpReadDenied = $firewallReadDenied
    if ($tcpReadDenied -or $udpReadDenied) {
        Add-Result "WARN firewall rules not readable by this PowerShell; Android HTTP reachability will verify access"
        return
    }
    if ($tcpOk -and $udpOk) {
        Add-Result "PASS app-scoped Windows firewall rules already present"
        return
    }

    if (-not (Test-IsAdmin)) {
        throw "Firewall rules missing and PowerShell is not elevated. Run .\output\dlna-server.exe --configure-firewall --port $serverPort once as administrator, or rerun this script from an elevated PowerShell."
    }

    $proc = Start-Process -FilePath $exePath -ArgumentList @("--configure-firewall", "--port", "$serverPort") -Wait -PassThru -WindowStyle Hidden
    if ($proc.ExitCode -ne 0) {
        throw "Elevated firewall helper failed with exit code $($proc.ExitCode)"
    }

    $tcpOk = Test-DlnaFirewallRule "dlna-server HTTP TCP" $exePath "TCP" 0
    $udpOk = Test-DlnaFirewallRule "dlna-server SSDP UDP" $exePath "UDP" $ssdpPort
    if (-not ($tcpOk -and $udpOk)) {
        throw "Firewall helper completed but required rules were not found"
    }
    Add-Result "PASS app-scoped Windows firewall rules configured"
}

function New-TestWav([string]$path) {
    $sampleRate = 8000
    $durationSeconds = 3
    $sampleCount = $sampleRate * $durationSeconds
    $dataBytes = $sampleCount * 2
    $fs = [IO.File]::Open($path, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::None)
    $bw = New-Object IO.BinaryWriter($fs)
    try {
        $bw.Write([Text.Encoding]::ASCII.GetBytes("RIFF"))
        $bw.Write([int](36 + $dataBytes))
        $bw.Write([Text.Encoding]::ASCII.GetBytes("WAVE"))
        $bw.Write([Text.Encoding]::ASCII.GetBytes("fmt "))
        $bw.Write([int]16)
        $bw.Write([int16]1)
        $bw.Write([int16]1)
        $bw.Write([int]$sampleRate)
        $bw.Write([int]($sampleRate * 2))
        $bw.Write([int16]2)
        $bw.Write([int16]16)
        $bw.Write([Text.Encoding]::ASCII.GetBytes("data"))
        $bw.Write([int]$dataBytes)
        for ($i = 0; $i -lt $sampleCount; $i++) {
            $sample = [int16](3000 * [Math]::Sin((2 * [Math]::PI * 440 * $i) / $sampleRate))
            $bw.Write($sample)
        }
    } finally {
        $bw.Close()
        $fs.Close()
    }
}

function New-BrowseSoap([int]$objectId) {
    return @"
<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
  <s:Body>
    <u:Browse xmlns:u="urn:schemas-upnp-org:service:ContentDirectory:1">
      <ObjectID>$objectId</ObjectID>
      <BrowseFlag>BrowseDirectChildren</BrowseFlag>
      <Filter>*</Filter>
      <StartingIndex>0</StartingIndex>
      <RequestedCount>0</RequestedCount>
      <SortCriteria></SortCriteria>
    </u:Browse>
  </s:Body>
</s:Envelope>
"@
}

function Invoke-AndroidBrowse([string]$pcIp, [int]$objectId) {
    $soapPath = Join-Path $env:TEMP "dlna-browse-$objectId.xml"
    $utf8NoBom = New-Object Text.UTF8Encoding($false)
    [IO.File]::WriteAllText($soapPath, (New-BrowseSoap $objectId), $utf8NoBom)
    Invoke-AdbText @("push", $soapPath, "/data/local/tmp/dlna-browse.xml") | Out-Null
    $url = "http://$pcIp`:$serverPort/upnp/control/content_directory"
    $cmd = "curl -sS -m 8 -H 'Content-Type: text/xml; charset=utf-8' -H 'SOAPACTION: `"urn:schemas-upnp-org:service:ContentDirectory:1#Browse`"' --data-binary @/data/local/tmp/dlna-browse.xml '$url'"
    return Invoke-AdbText @("shell", $cmd)
}

function Find-FirstGroupValue([string]$text, [string[]]$patterns) {
    foreach ($pattern in $patterns) {
        $match = [regex]::Match($text, $pattern)
        if ($match.Success) {
            return $match.Groups[1].Value
        }
    }
    return ""
}

function Invoke-UiDump([string]$command, [string]$path) {
    $dump = Invoke-AdbText @("shell", $command)
    if ($dump -match "UI hierchary dumped|UI hierarchy dumped") {
        return Invoke-AdbText @("shell", "cat", $path)
    }
    throw $dump
}

function Get-UiXml {
    $lastError = ""
    $attempts = @(
        @{ Command = "uiautomator dump /sdcard/dlna-vlc-ui.xml"; Path = "/sdcard/dlna-vlc-ui.xml" },
        @{ Command = "uiautomator dump --compressed /sdcard/dlna-vlc-ui.xml"; Path = "/sdcard/dlna-vlc-ui.xml" },
        @{ Command = "uiautomator dump"; Path = "/sdcard/window_dump.xml" },
        @{ Command = "uiautomator dump --compressed"; Path = "/sdcard/window_dump.xml" }
    )

    for ($i = 0; $i -lt 6; $i++) {
        foreach ($attempt in $attempts) {
            try {
                return Invoke-UiDump $attempt.Command $attempt.Path
            } catch {
                $lastError = $_.Exception.Message
            }
        }
        Invoke-AdbText @("shell", "am", "broadcast", "-a", "android.intent.action.CLOSE_SYSTEM_DIALOGS") | Out-Null
        Start-Sleep -Seconds 1
    }

    throw "uiautomator dump failed after retries: $lastError"
}

function Invoke-TapUiText([string[]]$patterns) {
    $xml = Get-UiXml
    $nodes = [regex]::Matches($xml, "<node\b[^>]*>")
    foreach ($nodeMatch in $nodes) {
        $node = $nodeMatch.Value
        $text = Find-FirstGroupValue $node @('text="([^"]*)"')
        $desc = Find-FirstGroupValue $node @('content-desc="([^"]*)"')
        $label = "$text $desc"
        foreach ($pattern in $patterns) {
            if ($label -match $pattern) {
                $bounds = [regex]::Match($node, 'bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"')
                if ($bounds.Success) {
                    $x = [int](([int]$bounds.Groups[1].Value + [int]$bounds.Groups[3].Value) / 2)
                    $y = [int](([int]$bounds.Groups[2].Value + [int]$bounds.Groups[4].Value) / 2)
                    Invoke-AdbText @("shell", "input", "tap", "$x", "$y") | Out-Null
                    Start-Sleep -Seconds 2
                    return $true
                }
            }
        }
    }
    return $false
}

function Invoke-VlcLocalNetworkDiscovery([string]$androidIp, [string]$serverName) {
    $preLog = Read-DebugLog
    Invoke-AdbText @("shell", "am", "force-stop", "org.videolan.vlc") | Out-Null
    Invoke-AdbText @("shell", "monkey", "-p", "org.videolan.vlc", "-c", "android.intent.category.LAUNCHER", "1") | Out-Null
    Start-Sleep -Seconds 5

    for ($i = 0; $i -lt 4; $i++) {
        if (-not (Invoke-TapUiText @("(?i)\bnext\b", "(?i)\ballow\b", "(?i)\bdone\b", "(?i)\bskip\b"))) {
            break
        }
    }
    Invoke-TapUiText @("(?i)\bbrowse\b") | Out-Null
    Invoke-TapUiText @("(?i)local network", "(?i)\bnetwork\b") | Out-Null

    Start-Sleep -Seconds 20
    $postLog = Read-DebugLog
    $newLog = $postLog.Substring([Math]::Min($preLog.Length, $postLog.Length))
    if ($newLog -notmatch "SSDP search in: src=$([regex]::Escape($androidIp))") {
        throw "VLC Local Network did not trigger SSDP M-SEARCH from $androidIp"
    }
    if ($newLog -notmatch "SSDP response sent: dst=$([regex]::Escape($androidIp))") {
        throw "Server did not send SSDP response to VLC device $androidIp"
    }

    $ui = Get-UiXml
    if ($ui -match [regex]::Escape($serverName)) {
        Add-Result "PASS VLC Local Network UI shows $serverName"
    } else {
        Add-Result "PASS VLC Local Network triggered SSDP discovery and response"
    }
}

function Invoke-VlcPlayback([string]$androidIp, [string]$mediaUrl, [string]$mediaId) {
    $preLog = Read-DebugLog
    Invoke-AdbText @("shell", "am", "force-stop", "org.videolan.vlc") | Out-Null
    Invoke-AdbText @(
        "shell", "am", "start",
        "-a", "android.intent.action.VIEW",
        "-d", $mediaUrl,
        "-t", "audio/wav",
        "-n", "org.videolan.vlc/.StartActivity"
    ) | Out-Null
    Start-Sleep -Seconds 10

    $postLog = Read-DebugLog
    $newLog = $postLog.Substring([Math]::Min($preLog.Length, $postLog.Length))
    if ($newLog -match "HTTP request: src=$([regex]::Escape($androidIp)) method=(GET|HEAD) path=/media/$mediaId") {
        Add-Result "PASS Android VLC requested DLNA media /media/$mediaId from server"
    } else {
        throw "Android VLC did not request /media/$mediaId from server"
    }
}

try {
    Assert-Command "adb"
    Assert-Command "curl.exe"
    if (-not (Test-Path $exePath)) {
        throw "Missing built exe at $exePath"
    }

    $selectedDevice = Select-AdbDevice
    $script:adbTargetArgs = @("-s", $selectedDevice)
    if ((Invoke-AdbText @("shell", "pm", "list", "packages", "org.videolan.vlc")) -notmatch "org.videolan.vlc") {
        throw "Android VLC package org.videolan.vlc not installed"
    }
    Add-Result "PASS adb device $selectedDevice and Android VLC detected"

    $androidIp = Get-AndroidIPv4
    $pcIp = Get-PCAddressForAndroid $androidIp
    Add-Result "PASS Android wlan0=$androidIp Windows peer=$pcIp"
    Ensure-FirewallAccess
    Assert-PhoneUnlocked
    Add-Result "PASS Android phone is unlocked for VLC UI automation"

    New-Item -ItemType Directory -Path $appDataDir -Force | Out-Null
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
    if (Test-Path $configPath) {
        $backupPath = Join-Path $env:TEMP ("dlna-server-config-backup-" + [guid]::NewGuid().ToString() + ".ini")
        Copy-Item -LiteralPath $configPath -Destination $backupPath -Force
    }
    if (Test-Path $debugLogPath) {
        Remove-Item -LiteralPath $debugLogPath -Force
    }
    if (Test-Path $testMediaDir) {
        Remove-Item -LiteralPath $testMediaDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $testMediaDir | Out-Null
    New-TestWav (Join-Path $testMediaDir "android-smoke.wav")

    if (Test-Path $configPath) {
        Remove-Item -LiteralPath $configPath -Force
    }
    Set-IniValue "Settings" "ServerName" "WinDLNA Android Smoke"
    Set-IniValue "Settings" "Port" "$serverPort"
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
        $listen = Get-NetTCPConnection -State Listen -LocalPort $serverPort -ErrorAction SilentlyContinue
        if ($listen) {
            $listenReady = $true
            break
        }
    }
    if (-not $listenReady) {
        throw "Server did not listen on TCP $serverPort"
    }
    Add-Result "PASS server listening on TCP $serverPort"

    $desc = Invoke-AdbText @("shell", "curl", "-sS", "-m", "8", "http://$pcIp`:$serverPort/description.xml")
    if ($desc -match "WinDLNA Android Smoke" -and $desc -match "ContentDirectory:1") {
        Add-Result "PASS Android curl fetched description.xml from Windows server"
    } else {
        throw "Android curl description.xml missing expected DLNA XML"
    }

    $rootBrowse = Invoke-AndroidBrowse $pcIp 0
    $folderId = Find-FirstGroupValue $rootBrowse @('container id=&quot;(\d+)&quot;', 'container id="(\d+)"')
    if (-not $folderId) {
        throw "ContentDirectory root browse did not return a media folder"
    }
    $folderBrowse = Invoke-AndroidBrowse $pcIp ([int]$folderId)
    $mediaUrl = Find-FirstGroupValue $folderBrowse @('(http://[^&<\s"]+/media/(\d+))')
    if (-not $mediaUrl) {
        throw "ContentDirectory folder browse did not return a media URL"
    }
    $mediaId = Find-FirstGroupValue $mediaUrl @('/media/(\d+)')
    Add-Result "PASS Android SOAP Browse found media URL $mediaUrl"

    $head = Invoke-AdbText @("shell", "curl", "-sS", "-I", "-m", "8", $mediaUrl)
    if ($head -notmatch "200 OK" -or $head -notmatch "audio/wav") {
        throw "Android HEAD media response missing 200 OK audio/wav"
    }
    $rangeCmd = "curl -sS -m 8 -r 0-31 -o /dev/null -w '%{http_code} %{size_download}' '$mediaUrl'"
    $range = Invoke-AdbText @("shell", $rangeCmd)
    if ($range -notmatch "^206\s+32") {
        throw "Android range GET expected 206 and 32 bytes, got $range"
    }
    Add-Result "PASS Android curl verified media HEAD and byte-range GET"

    Invoke-VlcLocalNetworkDiscovery $androidIp "WinDLNA Android Smoke"
    Invoke-VlcPlayback $androidIp $mediaUrl $mediaId

    Set-Content -LiteralPath $debugCopyPath -Value (Read-DebugLog) -Encoding UTF8
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
}
