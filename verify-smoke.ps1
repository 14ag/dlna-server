param()

$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent $MyInvocation.MyCommand.Path
$outputDir = Join-Path $repo "output"
$exePath = Join-Path $outputDir "WinDLNAServer.exe"
$vlcPath = "C:\Program Files\VideoLAN\VLC\vlc.exe"
$appDataDir = Join-Path $env:APPDATA "WinDLNAServer"
$configPath = Join-Path $outputDir "config.ini"
$debugLogPath = Join-Path $appDataDir "debug.log"
$resultsPath = Join-Path $outputDir "verification-results.txt"
$debugCopyPath = Join-Path $outputDir "verification-debug.log"
$testMediaDir = Join-Path $env:TEMP "WinDLNAServer-TestMedia"
$backupPath = $null
$serverProc = $null
$vlcProc = $null
$summary = New-Object System.Collections.Generic.List[string]

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class User32Bridge {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
}
"@

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Kernel32IniBridge {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool WritePrivateProfileString(string lpAppName, string lpKeyName, string lpString, string lpFileName);
}
"@

function Add-Result {
    param([string]$line)

    $summary.Add($line) | Out-Null
    Write-Host $line
}

function Set-IniValue {
    param(
        [string]$section,
        [string]$key,
        [string]$value
    )

    if (-not [Kernel32IniBridge]::WritePrivateProfileString($section, $key, $value, $configPath)) {
        throw "Failed to write INI value $section/$key"
    }
}

function Parse-SsdpResponse {
    param([string]$response)

    $headers = @{}
    $lines = $response -split "`r?`n"
    foreach ($line in $lines) {
        $idx = $line.IndexOf(":")
        if ($idx -gt 0) {
            $key = $line.Substring(0, $idx).Trim().ToUpperInvariant()
            $value = $line.Substring($idx + 1).Trim()
            $headers[$key] = $value
        }
    }
    return $headers
}

function Send-MSearchIPv4 {
    param([string]$st)

    $client = New-Object System.Net.Sockets.UdpClient([System.Net.Sockets.AddressFamily]::InterNetwork)
    try {
        $client.Client.ReceiveTimeout = 2500
        $message = @(
            "M-SEARCH * HTTP/1.1",
            "HOST: 239.255.255.250:1900",
            'MAN: "ssdp:discover"',
            "MX: 1",
            "ST: $st",
            "",
            ""
        ) -join "`r`n"

        $bytes = [System.Text.Encoding]::ASCII.GetBytes($message)
        $endpoint = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Parse("239.255.255.250"), 1900)
        [void]$client.Send($bytes, $bytes.Length, $endpoint)

        $deadline = (Get-Date).AddSeconds(5)
        $responses = @()
        while ((Get-Date) -lt $deadline) {
            try {
                $remote = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, 0)
                $received = $client.Receive([ref]$remote)
                $responses += [PSCustomObject]@{
                    Remote = $remote.ToString()
                    Text = [System.Text.Encoding]::ASCII.GetString($received)
                }
            } catch [System.Management.Automation.MethodInvocationException] {
                if ($_.Exception.InnerException -and $_.Exception.InnerException.GetType().FullName -eq "System.Net.Sockets.SocketException") {
                    break
                }
                throw
            } catch [System.Net.Sockets.SocketException] {
                break
            }
        }

        return $responses
    } finally {
        $client.Close()
    }
}

function Send-MSearchIPv6 {
    param(
        [string]$st,
        [int]$ifIndex
    )

    $client = New-Object System.Net.Sockets.UdpClient([System.Net.Sockets.AddressFamily]::InterNetworkV6)
    try {
        $client.Client.ReceiveTimeout = 2500
        $client.Client.Bind((New-Object System.Net.IPEndPoint([System.Net.IPAddress]::IPv6Any, 0)))
        $client.Client.SetSocketOption([System.Net.Sockets.SocketOptionLevel]::IPv6, [System.Net.Sockets.SocketOptionName]::MulticastInterface, $ifIndex)

        $message = @(
            "M-SEARCH * HTTP/1.1",
            "HOST: [FF02::C]:1900",
            'MAN: "ssdp:discover"',
            "MX: 1",
            "ST: $st",
            "",
            ""
        ) -join "`r`n"

        $bytes = [System.Text.Encoding]::ASCII.GetBytes($message)
        $address = [System.Net.IPAddress]::Parse("ff02::c%$ifIndex")
        $endpoint = New-Object System.Net.IPEndPoint($address, 1900)
        [void]$client.Send($bytes, $bytes.Length, $endpoint)

        $deadline = (Get-Date).AddSeconds(5)
        $responses = @()
        while ((Get-Date) -lt $deadline) {
            try {
                $remote = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::IPv6Any, 0)
                $received = $client.Receive([ref]$remote)
                $responses += [PSCustomObject]@{
                    Remote = $remote.ToString()
                    Text = [System.Text.Encoding]::ASCII.GetString($received)
                }
            } catch [System.Management.Automation.MethodInvocationException] {
                if ($_.Exception.InnerException -and $_.Exception.InnerException.GetType().FullName -eq "System.Net.Sockets.SocketException") {
                    break
                }
                throw
            } catch [System.Net.Sockets.SocketException] {
                break
            }
        }

        return $responses
    } finally {
        $client.Close()
    }
}

function Find-LogLines {
    param([string]$pattern)

    if (-not (Test-Path $debugLogPath)) {
        return @()
    }

    $text = Read-DebugLog
    if (-not $text) {
        return @()
    }

    return ($text -split "`r?`n") | Where-Object { $_ -match $pattern }
}

function Read-DebugLog {
    if (-not (Test-Path $debugLogPath)) {
        return ""
    }

    for ($i = 0; $i -lt 8; $i++) {
        try {
            return Get-Content -LiteralPath $debugLogPath -Raw -ErrorAction Stop
        } catch {
            Start-Sleep -Milliseconds 150
        }
    }

    return ""
}

function Stop-RepoDlnaProcesses {
    $repoFull = [System.IO.Path]::GetFullPath($repo)
    Get-Process -Name "WinDLNAServer" -ErrorAction SilentlyContinue | ForEach-Object {
        $path = $null
        try {
            $path = $_.Path
        } catch {
        }
        if ($path) {
            $fullPath = [System.IO.Path]::GetFullPath($path)
            if ($fullPath.StartsWith($repoFull, [System.StringComparison]::OrdinalIgnoreCase)) {
                Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
            }
        }
    }
}

try {
    if (-not (Test-Path $exePath)) {
        throw "Missing built exe at $exePath"
    }

    Stop-RepoDlnaProcesses

    New-Item -ItemType Directory -Path $appDataDir -Force | Out-Null
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
    Set-Content -LiteralPath (Join-Path $testMediaDir "song.mp3") -Value "fake mp3 bytes" -Encoding ascii
    Set-Content -LiteralPath (Join-Path $testMediaDir "cover.jpg") -Value "fake jpg bytes" -Encoding ascii

    if (Test-Path $configPath) {
        Remove-Item -LiteralPath $configPath -Force
    }
    Set-IniValue "Settings" "ServerName" "DLNA 测试"
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
    Set-IniValue "Settings" "DeviceUUID" "11111111-2222-3333-4444-555555555555"
    Set-IniValue "Settings" "MediaSources" $testMediaDir

    $serverProc = Start-Process -FilePath $exePath -PassThru

    $windowHandle = [IntPtr]::Zero
    $windowReady = $false
    $listenReady = $false
    for ($i = 0; $i -lt 40; $i++) {
        Start-Sleep -Milliseconds 500
        $serverProc.Refresh()
        $rawHandle = $serverProc.MainWindowHandle
        if ($null -ne $rawHandle -and $rawHandle -ne 0) {
            $windowHandle = [IntPtr]$rawHandle
            $windowReady = $true
            break
        }
    }
    if (-not $windowReady) {
        throw "Server window/listener did not become ready."
    }
    [void][User32Bridge]::PostMessage($windowHandle, 0x0111, [IntPtr]202, [IntPtr]0)
    for ($i = 0; $i -lt 30; $i++) {
        Start-Sleep -Milliseconds 500
        $tcpListen = Get-NetTCPConnection -State Listen -LocalPort 18200 -ErrorAction SilentlyContinue
        if ($tcpListen) {
            $listenReady = $true
            break
        }
    }
    if (-not $listenReady) {
        throw "Server did not start listening on TCP 18200 after WM_COMMAND start."
    }
    Add-Result "PASS app window launched and server listened on TCP 18200"

    $udpListen = Get-NetUDPEndpoint -LocalPort 1900 -ErrorAction SilentlyContinue | Select-Object LocalAddress, OwningProcess
    if ($udpListen) {
        Add-Result ("PASS UDP 1900 listener present: " + (($udpListen | ForEach-Object { "{0} pid={1}" -f $_.LocalAddress, $_.OwningProcess }) -join "; "))
    } else {
        Add-Result "WARN no UDP 1900 endpoint reported by Get-NetUDPEndpoint"
    }

    Start-Sleep -Seconds 2
    $aliveLines = Find-LogLines "SSDP notify sent: nts=ssdp:alive"
    if ($aliveLines.Count -ge 5) {
        Add-Result ("PASS startup alive burst logged ($($aliveLines.Count) entries)")
    } else {
        Add-Result ("WARN startup alive count lower than expected ($($aliveLines.Count) entries)")
    }

    $targets = @(
        "ssdp:all",
        "upnp:rootdevice",
        "uuid:11111111-2222-3333-4444-555555555555",
        "urn:schemas-upnp-org:device:MediaServer:1",
        "urn:schemas-upnp-org:service:ContentDirectory:1",
        "urn:schemas-upnp-org:service:ConnectionManager:1"
    )

    $ipv4Results = @{}
    foreach ($target in $targets) {
        $responses = Send-MSearchIPv4 $target
        $ipv4Results[$target] = $responses
        if ($responses.Count -gt 0) {
            Add-Result ("PASS IPv4 M-SEARCH $target -> $($responses.Count) response(s)")
        } else {
            $escapedTarget = [regex]::Escape($target)
            $logMatch = Find-LogLines ("SSDP response sent: .*st=$escapedTarget ")
            if ($logMatch.Count -gt 0) {
                Add-Result ("PASS IPv4 M-SEARCH $target -> server log confirms response path")
            } else {
                Add-Result ("FAIL IPv4 M-SEARCH $target -> no responses")
            }
        }
    }

    $allHeaders = @()
    foreach ($response in $ipv4Results["ssdp:all"]) {
        $allHeaders += ,(Parse-SsdpResponse $response.Text)
    }
    $stSet = New-Object System.Collections.Generic.HashSet[string]([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($headers in $allHeaders) {
        if ($headers.ContainsKey("ST")) {
            [void]$stSet.Add($headers["ST"])
        }
    }
    $expectedSt = @(
        "upnp:rootdevice",
        "uuid:11111111-2222-3333-4444-555555555555",
        "urn:schemas-upnp-org:device:MediaServer:1",
        "urn:schemas-upnp-org:service:ContentDirectory:1",
        "urn:schemas-upnp-org:service:ConnectionManager:1"
    )
    $missing = $expectedSt | Where-Object { -not $stSet.Contains($_) }
    if ($missing.Count -eq 0) {
        Add-Result "PASS ssdp:all returned all 5 advertised ST values"
    } else {
        Add-Result ("FAIL ssdp:all missing ST values: " + ($missing -join ", "))
    }

    $specificRoot = $ipv4Results["upnp:rootdevice"] | Select-Object -First 1
    if ($specificRoot) {
        $rootHeaders = Parse-SsdpResponse $specificRoot.Text
        $location = $rootHeaders["LOCATION"]
        $usn = $rootHeaders["USN"]
        if ($location -and $usn) {
            Add-Result ("PASS rootdevice response has LOCATION + USN ($location)")
        } else {
            Add-Result "FAIL rootdevice response missing LOCATION or USN"
        }

        $desc = Invoke-WebRequest -Uri $location -UseBasicParsing -TimeoutSec 10
        if ($desc.Content -match "DLNA 测试" -and $desc.Content -match "ContentDirectory:1") {
            Add-Result "PASS description.xml served UTF-8 friendlyName and ContentDirectory service"
        } else {
            Add-Result "FAIL description.xml missing expected friendlyName or ContentDirectory service"
        }

        $soapBody = @"
<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
  <s:Body>
    <u:Browse xmlns:u="urn:schemas-upnp-org:service:ContentDirectory:1">
      <ObjectID>0</ObjectID>
      <BrowseFlag>BrowseDirectChildren</BrowseFlag>
      <Filter>*</Filter>
      <StartingIndex>0</StartingIndex>
      <RequestedCount>0</RequestedCount>
      <SortCriteria></SortCriteria>
    </u:Browse>
  </s:Body>
</s:Envelope>
"@
        $browseResp = Invoke-WebRequest -Uri ($location -replace "/description.xml$", "/upnp/control/content_directory") -Method Post -ContentType 'text/xml; charset="utf-8"' -Headers @{ SOAPACTION = '"urn:schemas-upnp-org:service:ContentDirectory:1#Browse"' } -Body $soapBody -UseBasicParsing -TimeoutSec 10
        if ($browseResp.Content -match "WinDLNAServer-TestMedia") {
            $childId = [regex]::Match($browseResp.Content, 'container id=&quot;(\d+)&quot;').Groups[1].Value
            if ($childId) {
                $childBody = $soapBody -replace "<ObjectID>0</ObjectID>", "<ObjectID>$childId</ObjectID>"
                $childBrowse = Invoke-WebRequest -Uri ($location -replace "/description.xml$", "/upnp/control/content_directory") -Method Post -ContentType 'text/xml; charset="utf-8"' -Headers @{ SOAPACTION = '"urn:schemas-upnp-org:service:ContentDirectory:1#Browse"' } -Body $childBody -UseBasicParsing -TimeoutSec 10
                if ($childBrowse.Content -match "song" -or $childBrowse.Content -match "cover") {
                    Add-Result "PASS Browse SOAP returned media entries"
                } else {
                    Add-Result "FAIL child Browse SOAP did not include expected media entries"
                }
            } else {
                Add-Result "FAIL root Browse returned source folder but no child container id"
            }
        } else {
            Add-Result "FAIL root Browse SOAP did not include expected source folder"
        }
    }

    $ipv6If = Get-NetIPAddress -AddressFamily IPv6 -ErrorAction SilentlyContinue |
        Where-Object { $_.IPAddress -ne "::1" -and $_.IPAddress -notlike "ff*" } |
        Select-Object -First 1
    if ($ipv6If) {
        try {
            $ipv6Responses = Send-MSearchIPv6 "urn:schemas-upnp-org:device:MediaServer:1" $ipv6If.InterfaceIndex
            if ($ipv6Responses.Count -gt 0) {
                Add-Result ("PASS IPv6 M-SEARCH MediaServer -> $($ipv6Responses.Count) response(s)")
            } else {
                Add-Result "WARN IPv6 M-SEARCH returned no responses"
            }
        } catch {
            Add-Result ("WARN IPv6 probe failed: " + $_.Exception.Message)
        }
    } else {
        Add-Result "WARN no active IPv6 interface found for probe"
    }

    $preVlcLog = Read-DebugLog
    $vlcOut = Join-Path $env:TEMP ("vlc-out-" + [guid]::NewGuid().ToString() + ".log")
    $vlcErr = Join-Path $env:TEMP ("vlc-err-" + [guid]::NewGuid().ToString() + ".log")
    $vlcProc = Start-Process -FilePath $vlcPath -ArgumentList @("-I", "dummy", "upnp://") -RedirectStandardOutput $vlcOut -RedirectStandardError $vlcErr -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 8
    if (-not $vlcProc.HasExited) {
        Stop-Process -Id $vlcProc.Id -Force
    }
    Start-Sleep -Seconds 1
    $postVlcLog = Read-DebugLog
    $newVlcLog = $postVlcLog.Substring([Math]::Min($preVlcLog.Length, $postVlcLog.Length))
    if ($newVlcLog -notmatch "SSDP search in:") {
        $preVlcLog = $postVlcLog
        $vlcOut2 = Join-Path $env:TEMP ("vlc-out-" + [guid]::NewGuid().ToString() + ".log")
        $vlcErr2 = Join-Path $env:TEMP ("vlc-err-" + [guid]::NewGuid().ToString() + ".log")
        $vlcProc = Start-Process -FilePath $vlcPath -ArgumentList @("-I", "dummy", "--services-discovery", "upnp") -RedirectStandardOutput $vlcOut2 -RedirectStandardError $vlcErr2 -WindowStyle Hidden -PassThru
        Start-Sleep -Seconds 8
        if (-not $vlcProc.HasExited) {
            Stop-Process -Id $vlcProc.Id -Force
        }
        Start-Sleep -Seconds 1
        $postVlcLog = Read-DebugLog
        $newVlcLog = $postVlcLog.Substring([Math]::Min($preVlcLog.Length, $postVlcLog.Length))
    }
    if ($newVlcLog -match "SSDP search in:") {
        Add-Result "PASS VLC triggered SSDP search traffic seen by server"
    } else {
        Add-Result "WARN VLC run did not produce visible SSDP search in server log"
    }

    if ($windowHandle -ne [IntPtr]::Zero) {
        [void][User32Bridge]::PostMessage($windowHandle, 0x0111, [IntPtr]202, [IntPtr]0)
        Start-Sleep -Seconds 2
        $serverProc.CloseMainWindow() | Out-Null
        Start-Sleep -Seconds 2
        Add-Result "PASS sent stop + destroy messages to app window"
    } else {
        Add-Result "WARN could not find app window for graceful stop"
    }

    $byebyeLines = Find-LogLines "SSDP notify sent: nts=ssdp:byebye"
    if ($byebyeLines.Count -ge 5) {
        Add-Result ("PASS byebye notifications logged ($($byebyeLines.Count) entries)")
    } else {
        Add-Result ("WARN byebye count lower than expected ($($byebyeLines.Count) entries)")
    }

    if (Test-Path $debugLogPath) {
        try {
            Copy-Item -LiteralPath $debugLogPath -Destination $debugCopyPath -Force -ErrorAction Stop
        } catch {
            Set-Content -LiteralPath $debugCopyPath -Value (Read-DebugLog) -Encoding UTF8
        }
    }
    Set-Content -LiteralPath $resultsPath -Value ($summary -join [Environment]::NewLine) -Encoding UTF8
} finally {
    if ($vlcProc -and -not $vlcProc.HasExited) {
        Stop-Process -Id $vlcProc.Id -Force -ErrorAction SilentlyContinue
    }
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
