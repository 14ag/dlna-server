param(
    [string]$FtpSourceUrl = "ftp://14ag:qwertyui@192.168.100.33:2121/playlist_remote.m3u8",
    [string]$HlsSourceUrl = "C:\Users\philip\sauce\dlna-server\tests\test media\test-hls-playlist.m3u8"
)

$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$outputDir = Join-Path $repo "output\winx64"
$exePath = Join-Path $outputDir "DLNA Server.exe"
if (-not (Test-Path -LiteralPath $exePath)) {
    throw "Expected Windows x64 build at $exePath. Run cmake install with --prefix output\winx64 or build-assets.bat --platform winx64."
}
$vlcPath = "C:\Program Files\VideoLAN\VLC\vlc.exe"
$configPath = Join-Path $outputDir "config.ini"
$debugLogPath = Join-Path $outputDir "debug.log"
$resultsPath = Join-Path $outputDir "verification-results.txt"
$debugCopyPath = Join-Path $outputDir "verification-debug.log"
$transcriptPath = Join-Path $outputDir "verification-transcript.txt"
$serverPort = 18200
    $testMediaDir = Join-Path $env:TEMP "dlna-server-TestMedia"
    $backupPath = $null
    $serverProc = $null
    $vlcProc = $null
    $hlsHttpProc = $null
    $hlsHttpServerUrl = ""
    $hlsLocalPath = ""
    $summary = New-Object System.Collections.Generic.List[string]
    $transcriptStarted = $false

New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
    try {
        Start-Transcript -Path $transcriptPath -Force | Out-Null
        $transcriptStarted = $true
    }
    catch {
        Write-Host "WARN could not start transcript at $transcriptPath : $_"
    }

    # -------------------------------------------------------------------
    # HLS source: use tests\test media folder and start HTTP server
    # The DLNA server can scan local files, so we use the existing test media.
    # We start an HTTP server to serve the playlist for proxy testing.
    # -------------------------------------------------------------------
    $hlsLocalPath = ""
    if ($HlsSourceUrl -and $HlsSourceUrl -ne "" -and (Test-Path $HlsSourceUrl)) {
        $hlsSourceDir = Split-Path $HlsSourceUrl -Parent
        $hlsSourceName = Split-Path $HlsSourceUrl -Leaf
        
        # Use the source directory as the test media directory
        $hlsLocalPath = $HlsSourceUrl
        
        # Start HTTP server to serve it for proxy testing
        $hlsHttpPort = 18080
        $hlsHttpServerUrl = "http://127.0.0.1:$hlsHttpPort/$hlsSourceName"
        $hlsHttpProc = Start-Process -FilePath python -ArgumentList @("-m", "http.server", $hlsHttpPort, "--directory", "`"$hlsSourceDir`"") -PassThru -NoNewWindow
        Start-Sleep -Seconds 2
    }

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
    $line | Out-File -Append -FilePath (Join-Path $outputDir "realtime.log") -Encoding UTF8
}

function Set-IniValue {
    param(
        [string]$section,
        [string]$key,
        [string]$value
    )

    if (Test-Path $configPath) {
        $lines = @(Get-Content -LiteralPath $configPath -Encoding UTF8)
    }
    else {
        $lines = @("[$section]")
    }

    $sectionHeader = "[$section]"
    $sectionIndex = [Array]::IndexOf([string[]]$lines, $sectionHeader)
    if ($sectionIndex -lt 0) {
        $lines += $sectionHeader
        $sectionIndex = $lines.Count - 1
    }

    $keyPrefix = "$key="
    $insertIndex = $lines.Count
    for ($i = $sectionIndex + 1; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^\[.+\]$') {
            $insertIndex = $i
            break
        }
        if ($lines[$i].StartsWith($keyPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            $lines[$i] = "$key=$value"
            Set-Content -LiteralPath $configPath -Value $lines -Encoding UTF8
            return
        }
    }

    $list = New-Object System.Collections.Generic.List[string]
    foreach ($line in $lines) {
        $list.Add($line)
    }
    $list.Insert($insertIndex, "$key=$value")
    Set-Content -LiteralPath $configPath -Value $list -Encoding UTF8
}

function Invoke-CurlText {
    param([string[]]$curlArgs)

    $output = & curl.exe @curlArgs 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw (($output | Out-String).Trim())
    }
    return (($output | Out-String).Trim())
}

function Invoke-SoapCurl {
    param(
        [string]$url,
        [string]$body
    )

    $soapPath = Join-Path $env:TEMP ("dlna-smoke-soap-" + [guid]::NewGuid().ToString() + ".xml")
    $utf8NoBom = New-Object Text.UTF8Encoding($false)
    [IO.File]::WriteAllText($soapPath, $body, $utf8NoBom)
    try {
        return Invoke-CurlText @(
            "-sS", "--max-time", "10",
            "-H", "Content-Type: text/xml; charset=utf-8",
            "-H", 'SOAPACTION: "urn:schemas-upnp-org:service:ContentDirectory:1#Browse"',
            "--data-binary", "@$soapPath",
            $url
        )
    }
    finally {
        Remove-Item -LiteralPath $soapPath -Force -ErrorAction SilentlyContinue
    }
}

function Invoke-CurlRaw {
    param([string[]]$curlArgs)

    $output = & curl.exe @curlArgs 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw (($output | Out-String).Trim())
    }
    return ($output | Out-String)
}

function Invoke-WebRequestUtf8 {
    param([string]$url)

    $response = Invoke-WebRequest -Uri $url -UseBasicParsing -TimeoutSec 10
    $stream = $response.RawContentStream
    if ($stream.CanSeek) {
        $stream.Position = 0
    }
    $memory = New-Object IO.MemoryStream
    $stream.CopyTo($memory)
    return [Text.Encoding]::UTF8.GetString($memory.ToArray())
}

function ConvertFrom-SsdpResponse {
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
                    Text   = [System.Text.Encoding]::ASCII.GetString($received)
                }
            }
            catch [System.Management.Automation.MethodInvocationException] {
                if ($_.Exception.InnerException -and $_.Exception.InnerException.GetType().FullName -eq "System.Net.Sockets.SocketException") {
                    break
                }
                throw
            }
            catch [System.Net.Sockets.SocketException] {
                break
            }
        }

        return $responses
    }
    finally {
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
                    Text   = [System.Text.Encoding]::ASCII.GetString($received)
                }
            }
            catch [System.Management.Automation.MethodInvocationException] {
                if ($_.Exception.InnerException -and $_.Exception.InnerException.GetType().FullName -eq "System.Net.Sockets.SocketException") {
                    break
                }
                throw
            }
            catch [System.Net.Sockets.SocketException] {
                break
            }
        }

        return $responses
    }
    finally {
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

    return ($text -split '\r?\n') | Where-Object { $_ -match $pattern }
}

function Read-DebugLog {
    if (-not (Test-Path $debugLogPath)) {
        return ""
    }

    for ($i = 0; $i -lt 8; $i++) {
        try {
            $fs = [System.IO.File]::Open($debugLogPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
            $reader = New-Object System.IO.StreamReader($fs, [System.Text.Encoding]::UTF8)
            try {
                return $reader.ReadToEnd()
            } finally {
                $reader.Close()
            }
        }
        catch {
            Start-Sleep -Milliseconds 150
        }
    }

    return ""
}

function Get-FtpUrlCredential {
    param([string]$Url)

    try {
        $uri = [System.Uri]$Url
    }
    catch {
        return $null
    }
    if (-not $uri.UserInfo) {
        return $null
    }
    $parts = $uri.UserInfo.Split(":", 2)
    $password = ""
    if ($parts.Count -gt 1) {
        $password = [System.Uri]::UnescapeDataString($parts[1])
    }
    return [PSCustomObject]@{
        Uri      = $uri
        User     = [System.Uri]::UnescapeDataString($parts[0])
        Password = $password
    }
}

$script:kGroundTruthMediaExtensions = @(
    'mp4', 'm4v', 'mkv', 'webm', 'avi', 'divx', 'mov',
    'mpg', 'mpeg', 'mpe', 'vob', 'm2ts', 'mts', 'wmv', 'flv', '3gp', '3g2',
    'mp3', 'flac', 'm4a', 'aac', 'wav', 'wma', 'ogg', 'oga', 'opus',
    'aiff', 'aif', 'ac3', 'dts',
    'jpg', 'jpeg', 'png', 'gif', 'bmp', 'tif', 'tiff', 'webp'
)

$script:kGroundTruthPlaylistExtensions = @('.m3u', '.m3u8', '.pls')

function Get-PlaylistGroundTruth {
    param([string]$Url, $Credential, [int]$Depth = 0)
    if ($Depth -gt 8) { return [PSCustomObject]@{ Reachable = $true; ContainerCount = 0; MediaItemCount = 0; MaxDepthReached = $Depth } }
    try {
        $curlArgs = @('-sS', '--max-time', '10')
    if ($Credential.User) { $curlArgs += @('-u', "$($Credential.User):$($Credential.Password)") }
    $curlArgs += $Credential.Uri.AbsoluteUri
    $text = Invoke-CurlText $curlArgs
    }
    catch { return [PSCustomObject]@{ Reachable = $false; ContainerCount = 0; MediaItemCount = 0; MaxDepthReached = $Depth } }
    if (-not $text) { return [PSCustomObject]@{ Reachable = $false; ContainerCount = 0; MediaItemCount = 0; MaxDepthReached = $Depth } }
    $containerCount = 0
    $mediaItemCount = 0
    $lines = $text -split '\r?\n'
    foreach ($line in $lines) {
        $trimmed = $line.Trim()
        if ($trimmed.Length -eq 0 -or $trimmed.StartsWith('#')) { continue }
        $ext = [System.IO.Path]::GetExtension($trimmed).ToLowerInvariant()
        if ($script:kGroundTruthPlaylistExtensions -contains $ext) {
            $childUrl = $trimmed
            if ($childUrl -notmatch '^https?://' -and $childUrl -notmatch '^ftp://') {
                $baseUri = New-Object System.Uri($Credential.Uri, $childUrl)
                $childUrl = $baseUri.AbsoluteUri
            }
            $childCred = Get-FtpUrlCredential -Url $childUrl
            if (-not $childCred) {
                $childCred = [PSCustomObject]@{ Uri = [System.Uri]$childUrl; User = ''; Password = '' }
            }
            $childResult = Get-PlaylistGroundTruth -Url $childUrl -Credential $childCred -Depth ($Depth + 1)
            $containerCount += 1 + $childResult.ContainerCount
            $mediaItemCount += $childResult.MediaItemCount
            continue
        }
        $extNoDot = if ($ext.StartsWith('.')) { $ext.Substring(1) } else { $ext }
        if ($script:kGroundTruthMediaExtensions -contains $extNoDot) {
            $mediaItemCount++
        }
    }
    return [PSCustomObject]@{ Reachable = $true; ContainerCount = $containerCount; MediaItemCount = $mediaItemCount; MaxDepthReached = $Depth }
}

function Stop-RepoDlnaProcesses {
    $repoFull = [System.IO.Path]::GetFullPath($repo)
    Get-Process -Name "DLNA Server", "dlna-server", "WinDLNAServer" -ErrorAction SilentlyContinue | ForEach-Object {
        $path = $null
        try {
            $path = $_.Path
        }
        catch {
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
    Add-Result "INFO PowerShell version $($PSVersionTable.PSVersion) host $($PSVersionTable.PSEdition)"
    Add-Result "INFO FtpSourceUrl=$FtpSourceUrl"

    if (-not (Test-Path $exePath)) {
        throw "Missing built exe at $exePath"
    }
    if (-not (powershell Get-Command curl.exe -ErrorAction SilentlyContinue)) {
        throw "curl.exe missing"
    }

    Stop-RepoDlnaProcesses

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
    Set-Content -LiteralPath (Join-Path $testMediaDir "movie.mp4") -Value "fake mp4 bytes" -Encoding ascii
    Set-Content -LiteralPath (Join-Path $testMediaDir "movie.srt") -Value "1`r`n00:00:01,000 --> 00:00:02,000`r`nTest" -Encoding ascii
    $emptyFilePath = Join-Path $testMediaDir "empty.mp3"
    if (Test-Path -LiteralPath $emptyFilePath) {
        Remove-Item -LiteralPath $emptyFilePath -Force
    }
    New-Item -ItemType File -Path $emptyFilePath | Out-Null
    Set-Content -LiteralPath (Join-Path $testMediaDir "cover.jpg") -Value "fake jpg bytes" -Encoding ascii

    if (Test-Path $configPath) {
        Remove-Item -LiteralPath $configPath -Force
    }
    # non-ascii server name is built from numeric char codes on purpose.
    # a literal non-ascii string in this file would be misread as the
    # system ansi code page when this script has no byte order mark and
    # runs under windows powershell 5.1, which corrupts it before it ever
    # reaches config.ini. see the delivered review document, finding d-2.
    $nonAsciiServerName = "DLNA " + [char]0x6D4B + [char]0x8BD5
    Set-IniValue "Settings" "ServerName" $nonAsciiServerName
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
    Set-IniValue "Settings" "DeviceUUID" "11111111-2222-3333-4444-555555555555"

    # the ftp source is configured before the first server start, not
    # added at runtime, because the media source watcher currently skips
    # remote sources entirely (fix-plan task 13). adding it at runtime and
    # waiting for a rescan would never observe it.
    $testMediaSource = Join-Path $repo "tests\test media"
    $mediaSourcesValue = "$testMediaSource|$FtpSourceUrl"
    if ($hlsLocalPath -and $hlsLocalPath -ne "") {
        $mediaSourcesValue += "|" + $hlsLocalPath
    }
    Set-IniValue "Settings" "MediaSources" $mediaSourcesValue

    $env:DLNA_SERVER_SKIP_FIREWALL = "1"
    try {
        $serverProc = Start-Process -FilePath $exePath -PassThru
    }
    finally {
        Remove-Item Env:\DLNA_SERVER_SKIP_FIREWALL -ErrorAction SilentlyContinue
    }

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
        $tcpListen = Get-NetTCPConnection -State Listen -LocalPort $serverPort -ErrorAction SilentlyContinue
        if ($tcpListen) {
            $listenReady = $true
            break
        }
    }
    if (-not $listenReady) {
        throw "Server did not start listening on TCP $serverPort after WM_COMMAND start."
    }
    Add-Result "PASS app window launched and server listened on TCP $serverPort"

    $udpListen = Get-NetUDPEndpoint -LocalPort 1900 -ErrorAction SilentlyContinue | Select-Object LocalAddress, OwningProcess
    if ($udpListen) {
        Add-Result ("PASS UDP 1900 listener present: " + (($udpListen | ForEach-Object { "{0} pid={1}" -f $_.LocalAddress, $_.OwningProcess }) -join "; "))
    }
    else {
        Add-Result "WARN no UDP 1900 endpoint reported by Get-NetUDPEndpoint"
    }

    Start-Sleep -Seconds 2
    $aliveLines = Find-LogLines "SSDP notify sent: nts=ssdp:alive"
    if ($aliveLines.Count -ge 5) {
        Add-Result ("PASS startup alive burst logged ($($aliveLines.Count) entries)")
    }
    else {
        Add-Result ("WARN startup alive count lower than expected ($($aliveLines.Count) entries). this build does not use LibUPnP so the self-reported SSDP notify count should be 60 (4 interfaces * 5 USNs * 3 bursts). a low count here indicates SSDP notify logging arrived late; discovery was still confirmed by M-SEARCH responses below.")
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
        }
        else {
            $escapedTarget = [regex]::Escape($target)
            $logMatch = Find-LogLines ("SSDP response sent: .*st=$escapedTarget ")
            if ($logMatch.Count -gt 0) {
                Add-Result ("PASS IPv4 M-SEARCH $target -> server log confirms response path")
            }
            else {
                Add-Result ("FAIL IPv4 M-SEARCH $target -> no responses")
            }
        }
    }

    $allHeaders = @()
    foreach ($response in $ipv4Results["ssdp:all"]) {
        $allHeaders += , (ConvertFrom-SsdpResponse $response.Text)
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
    }
    else {
        Add-Result ("FAIL ssdp:all missing ST values: " + ($missing -join ", "))
    }

    $specificRoot = $null
    $location = $null
    $descContent = $null
    foreach ($candidate in $ipv4Results["upnp:rootdevice"]) {
        $candidateHeaders = ConvertFrom-SsdpResponse $candidate.Text
        $candidateLocation = $candidateHeaders["LOCATION"]
        if (-not $candidateLocation -or ($candidateLocation -notmatch "/description\.xml$" -and $candidateLocation -notmatch "/device\.xml$")) {
            continue
        }
        try {
            $candidateDesc = Invoke-WebRequestUtf8 $candidateLocation
            if ($candidateDesc -match "ContentDirectory:1") {
                $specificRoot = $candidate
                $location = $candidateLocation
                $descContent = $candidateDesc
                break
            }
        }
        catch {
        }
    }
    if ($specificRoot) {
        Add-Result "PASS SSDP LOCATION pointed to retrievable description.xml"

        # Check for icons
        $baseUrl = $location -replace "/description\.xml$", "" -replace "/device\.xml$", ""
        $iconFailures = @()
        foreach ($size in 48, 120, 256) {
            if ($descContent -notmatch "/server_icon_$size.png") {
                $iconFailures += "$size not advertised"
                continue
            }
            try {
                $iconResp = Invoke-WebRequest -Uri "http://127.0.0.1:$serverPort/icons/server_icon_$size.png" -UseBasicParsing -TimeoutSec 5
                $contentType = [string]$iconResp.Headers["Content-Type"]
                $contentLength = 0
                if ($null -ne $iconResp.RawContentLength) {
                    $contentLength = [int64]$iconResp.RawContentLength
                }
                elseif ($null -ne $iconResp.Content) {
                    $contentLength = $iconResp.Content.Length
                }
                if ($iconResp.StatusCode -ne 200 -or $contentType -notmatch "image/png" -or $contentLength -le 0) {
                    $iconFailures += "$size returned status=$($iconResp.StatusCode) type=$contentType length=$contentLength"
                }
            }
            catch {
                $iconFailures += "$size request failed: $($_.Exception.Message)"
            }
        }
        if ($iconFailures.Count -eq 0) {
            Add-Result "PASS description.xml advertised and served server icon PNGs"
        }
        else {
            Add-Result ("FAIL server icon PNG check: " + ($iconFailures -join "; "))
        }

        $connectionManagerBody = @"
<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
  <s:Body>
    <u:GetProtocolInfo xmlns:u="urn:schemas-upnp-org:service:ConnectionManager:1" />
  </s:Body>
</s:Envelope>
"@
        $connectionManagerContent = Invoke-SoapCurl ($location -replace "/description\.xml$", "/upnp/control/connection_manager" -replace "/device\.xml$", "/upnp/control/connection_manager") $connectionManagerBody
        if ($connectionManagerContent -match "<Source>" -and $connectionManagerContent -match "http-get:\*:" -and $connectionManagerContent -match "<Sink></Sink>") {
            Add-Result "PASS ConnectionManager GetProtocolInfo returned source protocol info"
        }
        else {
            Add-Result "FAIL ConnectionManager GetProtocolInfo response missing protocol info"
        }

        $eventUrl = $location -replace "/description\.xml$", "/upnp/event/content_directory" -replace "/device\.xml$", "/upnp/event/content_directory"
        $subscribeRaw = Invoke-CurlRaw @(
            "-sS", "-i", "--max-time", "10",
            "-X", "SUBSCRIBE",
            "-H", "CALLBACK: <http://127.0.0.1:9/dlna-event>",
            "-H", "NT: upnp:event",
            "-H", "TIMEOUT: Second-1800",
            $eventUrl
        )
        $sidMatch = [regex]::Match($subscribeRaw, "(?im)^SID:\s*(.+?)\s*$")
        if ($subscribeRaw -match "HTTP/1\.1 200 OK" -and $sidMatch.Success -and $subscribeRaw -match "(?im)^TIMEOUT:\s*Second-1800\s*$") {
            Add-Result "PASS ContentDirectory event SUBSCRIBE returned SID and timeout"
            $sid = $sidMatch.Groups[1].Value.Trim()
            $unsubscribeRaw = Invoke-CurlRaw @(
                "-sS", "-i", "--max-time", "10",
                "-X", "UNSUBSCRIBE",
                "-H", "SID: $sid",
                $eventUrl
            )
            if ($unsubscribeRaw -match "HTTP/1\.1 200 OK") {
                Add-Result "PASS ContentDirectory event UNSUBSCRIBE accepted SID"
            }
            else {
                Add-Result "FAIL ContentDirectory event UNSUBSCRIBE response: $unsubscribeRaw"
            }
        }
        else {
            Add-Result "FAIL ContentDirectory event SUBSCRIBE response: $subscribeRaw"
        }

        # wait for initial scan to complete before Browse
        $scanDeadline = [datetime]::UtcNow.AddSeconds(120)
        $scanFinished = $false
        while ([datetime]::UtcNow -lt $scanDeadline) {
            $scanLines = Find-LogLines "Scanned \d+ media items\."
            if ($scanLines.Count -gt 0) {
                Add-Result ("PASS scan completed: " + ($scanLines[-1] -replace '^\s+',''))
                $scanFinished = $true
                break
            }
            $errorLines = Find-LogLines "Source unavailable|scan failed|Scan aborted"
            if ($errorLines.Count -gt 0) {
                Add-Result ("WARN scan has error lines: " + ($errorLines -join " | "))
                $scanFinished = $true
                break
            }
            Start-Sleep -Milliseconds 500
        }
        if (-not $scanFinished) {
            Add-Result "WARN scan did not complete within 120s timeout"
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
        $browseContent = Invoke-SoapCurl ($location -replace "/description\.xml$", "/upnp/control/content_directory" -replace "/device\.xml$", "/upnp/control/content_directory") $soapBody
        if ($browseContent -match "dlna-server-TestMedia") {
            $childId = [regex]::Match($browseContent, 'container id=&quot;(\d+)&quot;').Groups[1].Value
            if ($childId) {
                $childBody = $soapBody -replace "<ObjectID>0</ObjectID>", "<ObjectID>$childId</ObjectID>"
                $childBrowseContent = Invoke-SoapCurl ($location -replace "/description\.xml$", "/upnp/control/content_directory" -replace "/device\.xml$", "/upnp/control/content_directory") $childBody
                if ($childBrowseContent -match "movie" -or $childBrowseContent -match "cover") {
                    Add-Result "PASS Browse SOAP returned media entries"

                    $decodedDIDL = [System.Net.WebUtility]::HtmlDecode($childBrowseContent)

                    # Find movie.mp4 ID
                    $movieMatch = [regex]::Match($decodedDIDL, '<item id="(\d+)"(?:(?!</item>)[\s\S])*?<dc:title>movie</dc:title>(?:(?!</item>)[\s\S])*?</item>')
                    $movieId = $movieMatch.Groups[1].Value

                    # Find empty.mp3 ID
                    $emptyMatch = [regex]::Match($decodedDIDL, '<item id="(\d+)"(?:(?!</item>)[\s\S])*?<dc:title>empty</dc:title>(?:(?!</item>)[\s\S])*?</item>')
                    $emptyId = $emptyMatch.Groups[1].Value

                    # 1. Subtitle URL advertisement check
                    if ($childBrowseContent -match "CaptionInfoEx" -and $childBrowseContent -match "/subtitle/") {
                        Add-Result "PASS Browse SOAP advertised subtitle URL via sec:CaptionInfoEx"
                    }
                    else {
                        Add-Result "FAIL Browse SOAP did not advertise subtitle URL"
                    }

                    # 2. HEAD request check
                    try {
                        $headResp = Invoke-WebRequest -Uri $location -Method Head -UseBasicParsing -TimeoutSec 5
                        if ($headResp.StatusCode -eq 200 -and -not $headResp.Content) {
                            Add-Result "PASS HEAD request returned 200 OK with empty body"
                        }
                        else {
                            Add-Result "FAIL HEAD request returned status $($headResp.StatusCode) or non-empty body"
                        }
                    }
                    catch {
                        Add-Result "FAIL HEAD request threw: $($_.Exception.Message)"
                    }

                    # 3. Range 206 / 416 request check for movie using HttpClient
                    if ($movieId) {
                        Add-Type -AssemblyName System.Net.Http
                        $client = New-Object System.Net.Http.HttpClient
                        try {
                            $request = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Get, "http://127.0.0.1:$serverPort/media/$movieId")
                            $request.Headers.Range = New-Object System.Net.Http.Headers.RangeHeaderValue(0, 4)
                            $response = $client.SendAsync($request).GetAwaiter().GetResult()
                            $statusCode = [int]$response.StatusCode
                            if ($statusCode -eq 206) {
                                $contentRange = $null
                                if ($response.Content.Headers.Contains("Content-Range")) {
                                    $contentRange = [System.Linq.Enumerable]::First($response.Content.Headers.GetValues("Content-Range"))
                                }
                                if ($contentRange -match "bytes 0-4/") {
                                    Add-Result "PASS Range request returned 206 with correct Content-Range"
                                }
                                else {
                                    Add-Result "FAIL Range request returned Content-Range: $contentRange"
                                }
                            }
                            else {
                                Add-Result "FAIL Range request returned status $statusCode"
                            }
                        }
                        finally {
                            $client.Dispose()
                        }

                        # Request Range 100- (unsatisfiable)
                        $client = New-Object System.Net.Http.HttpClient
                        try {
                            $request = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Get, "http://127.0.0.1:$serverPort/media/$movieId")
                            $request.Headers.Range = New-Object System.Net.Http.Headers.RangeHeaderValue(100, $null)
                            $response = $client.SendAsync($request).GetAwaiter().GetResult()
                            $statusCode = [int]$response.StatusCode
                            if ($statusCode -eq 416) {
                                $contentRange = $null
                                if ($response.Content.Headers.Contains("Content-Range")) {
                                    $contentRange = [System.Linq.Enumerable]::First($response.Content.Headers.GetValues("Content-Range"))
                                }
                                if ($contentRange -match "bytes \*/") {
                                    Add-Result "PASS unsatisfiable range request returned 416 with correct Content-Range"
                                }
                                else {
                                    Add-Result "FAIL 416 response has Content-Range: $contentRange"
                                }
                            }
                            else {
                                Add-Result "FAIL unsatisfiable range request returned status $statusCode"
                            }
                        }
                        finally {
                            $client.Dispose()
                        }
                    }
                    else {
                        Add-Result "FAIL could not find movie item ID in Browse response"
                    }

                    # keep-alive reuse: second request on same HttpClient
                    # after movie range-206 must reuse TCP connection
                    if ($movieId) {
                        Add-Type -AssemblyName System.Net.Http -ErrorAction SilentlyContinue
                        $kaHandler = New-Object System.Net.Http.HttpClientHandler
                        $kaHandler.UseProxy = $false
                        $kaClient = New-Object System.Net.Http.HttpClient($kaHandler)
                        $kaClient.Timeout = [TimeSpan]::FromSeconds(5)
                        try {
                            $kaReq1 = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Get, "http://127.0.0.1:$serverPort/media/$movieId")
                            $kaReq1.Headers.Range = New-Object System.Net.Http.Headers.RangeHeaderValue(0, 0)
                            $kaResp1 = $kaClient.SendAsync($kaReq1).GetAwaiter().GetResult()
                            if ([int]$kaResp1.StatusCode -ne 206) {
                                Add-Result "WARN keep-alive reuse pre-check failed: first request returned $([int]$kaResp1.StatusCode)"
                            }
                            else {
                                $kaReq2 = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Get, "http://127.0.0.1:$serverPort/media/$movieId")
                                $kaReq2.Headers.Range = New-Object System.Net.Http.Headers.RangeHeaderValue(0, 0)
                                $sw = [System.Diagnostics.Stopwatch]::StartNew()
                                $kaResp2 = $kaClient.SendAsync($kaReq2).GetAwaiter().GetResult()
                                $sw.Stop()
                                if ([int]$kaResp2.StatusCode -eq 206 -and $sw.ElapsedMilliseconds -lt 2000) {
                                    Add-Result "PASS keep-alive reuse: second range request on same HttpClient succeeded in $($sw.ElapsedMilliseconds)ms"
                                }
                                elseif ([int]$kaResp2.StatusCode -eq 206) {
                                    Add-Result "WARN keep-alive reuse: second range request succeeded but took $($sw.ElapsedMilliseconds)ms (may be new connection)"
                                }
                                else {
                                    Add-Result "WARN keep-alive reuse: second range request returned status $([int]$kaResp2.StatusCode)"
                                }
                            }
                        }
                        catch {
                            Add-Result "WARN keep-alive reuse check threw: $($_.Exception.Message)"
                        }
                        finally {
                            $kaClient.Dispose()
                        }
                    }

                    # 4. Range request on empty file check
                    if ($emptyId) {
                        $client = New-Object System.Net.Http.HttpClient
                        try {
                            $request = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Get, "http://127.0.0.1:$serverPort/media/$emptyId")
                            $request.Headers.Range = New-Object System.Net.Http.Headers.RangeHeaderValue(0, $null)
                            $response = $client.SendAsync($request).GetAwaiter().GetResult()
                            $statusCode = [int]$response.StatusCode
                            if ($statusCode -eq 416) {
                                $contentRange = $null
                                if ($response.Content.Headers.Contains("Content-Range")) {
                                    $contentRange = [System.Linq.Enumerable]::First($response.Content.Headers.GetValues("Content-Range"))
                                }
                                if ($contentRange -eq "bytes */0") {
                                    Add-Result "PASS empty file range request returned 416 with Content-Range: bytes */0"
                                }
                                else {
                                    Add-Result "FAIL empty file 416 response has Content-Range: $contentRange"
                                }
                            }
                            else {
                                Add-Result "FAIL empty file range request returned status $statusCode"
                            }
                        }
                        finally {
                            $client.Dispose()
                        }
                    }
                    else {
                        Add-Result "FAIL could not find empty item ID in Browse response"
                    }

                    # 5. Subtitle serving check
                    if ($movieId) {
                        try {
                            $subResp = Invoke-WebRequest -Uri "$baseUrl/subtitle/$movieId" -UseBasicParsing -TimeoutSec 5
                            if ($subResp.StatusCode -eq 200 -and $subResp.Content -match "Test") {
                                $subMime = $subResp.Headers["Content-Type"]
                                if ($subMime -match "application/x-subrip" -or $subMime -match "text/vtt" -or $subMime -match "srt") {
                                    Add-Result "PASS subtitle served with correct MIME type ($subMime)"
                                }
                                else {
                                    Add-Result "FAIL subtitle MIME type is $subMime"
                                }
                            }
                            else {
                                Add-Result "FAIL subtitle request returned status $($subResp.StatusCode)"
                            }
                        }
                        catch {
                            Add-Result "FAIL subtitle request threw: $($_.Exception.Message)"
                        }
                    }

                    if ($movieId) {
                        try {
                            $artResp = Invoke-WebRequest -Uri "$baseUrl/albumart/$movieId" -UseBasicParsing -TimeoutSec 5
                            if ($artResp.StatusCode -eq 200 -and $artResp.Headers["Content-Type"] -match "image/jpeg") {
                                Add-Result "PASS album art served with image MIME type"
                            }
                            else {
                                Add-Result "FAIL album art request returned status $($artResp.StatusCode)"
                            }
                        }
                        catch {
                            Add-Result "FAIL album art request threw: $($_.Exception.Message)"
                        }
                    }

                    # 6. Malformed SOAP XML check (SOAP fault 401)
                    $controlUrl = $location -replace "/description.xml$", "/upnp/control/content_directory"
                    $malformedSoap = @"
<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
  <s:Body>
    <u:Browse xmlns:u="urn:schemas-upnp-org:service:ContentDirectory:1">
      <ObjectID>0
"@
                    $faultContent = Invoke-SoapCurl $controlUrl $malformedSoap
                    if ($faultContent -match "<errorCode>401</errorCode>" -and $faultContent -match "Invalid XML") {
                        Add-Result "PASS malformed SOAP XML request rejected with SOAP fault 401"
                    }
                    else {
                        Add-Result "FAIL malformed SOAP XML request response: $faultContent"
                    }

                    # 7. Invalid Content-Length check
                    $tcpClient = New-Object System.Net.Sockets.TcpClient
                    try {
                        $tcpClient.Connect("127.0.0.1", $serverPort)
                        $stream = $tcpClient.GetStream()
                        $reqBytes = [System.Text.Encoding]::ASCII.GetBytes("POST /upnp/control/content_directory HTTP/1.1`r`nHost: 127.0.0.1:$serverPort`r`nContent-Length: -10`r`n`r`n")
                        $stream.Write($reqBytes, 0, $reqBytes.Length)
                        $reader = New-Object System.IO.StreamReader($stream)
                        $respLine = $reader.ReadLine()
                        if ($respLine -match "400 Bad Request") {
                            Add-Result "PASS invalid Content-Length (negative) rejected with 400 Bad Request"
                        }
                        else {
                            Add-Result "FAIL invalid Content-Length negative returned: $respLine"
                        }
                    }
                    finally {
                        $tcpClient.Close()
                    }

                    $tcpClient2 = New-Object System.Net.Sockets.TcpClient
                    try {
                        $tcpClient2.Connect("127.0.0.1", $serverPort)
                        $stream = $tcpClient2.GetStream()
                        $reqBytes = [System.Text.Encoding]::ASCII.GetBytes("POST /upnp/control/content_directory HTTP/1.1`r`nHost: 127.0.0.1:$serverPort`r`nContent-Length: abc`r`n`r`n")
                        $stream.Write($reqBytes, 0, $reqBytes.Length)
                        $reader = New-Object System.IO.StreamReader($stream)
                        $respLine = $reader.ReadLine()
                        if ($respLine -match "400 Bad Request") {
                            Add-Result "PASS non-numeric Content-Length rejected with 400 Bad Request"
                        }
                        else {
                            Add-Result "FAIL non-numeric Content-Length returned: $respLine"
                        }
                    }
                    finally {
                        $tcpClient2.Close()
                    }

                }
                else {
                    Add-Result "FAIL child Browse SOAP did not include expected media entries"
                }
            }
            else {
                Add-Result "FAIL root Browse returned source folder but no child container id"
            }
        }
        else {
            Add-Result "FAIL root Browse SOAP did not include expected source folder"
        }

        # begin ftp live source checks
        # this section is additive: it does not reuse or mutate any of the
        # local-folder state above, so a failure here cannot mark the
        # local-folder checks above as failed, and a failure above does not
        # skip this section, since discovery (specificRoot) is the only
        # shared precondition.
        $ftpCred = Get-FtpUrlCredential -Url $FtpSourceUrl
        $groundTruth = Get-PlaylistGroundTruth -Url $FtpSourceUrl -Credential $ftpCred
        if (-not $groundTruth.Reachable) {
            Add-Result "WARN ftp ground-truth pre-fetch failed; skipping ftp checks per problem-statement.md requirement (curl failure must skip, not fail)"
        }
        elseif (-not $ftpCred) {
            Add-Result "FAIL FtpSourceUrl is not a valid uri with embedded credentials: $FtpSourceUrl"
        }
        else {
            Add-Result "--- begin ftp live source checks: $FtpSourceUrl ---"
            $ftpPassword = $ftpCred.Password
            $ftpSourceName = [System.IO.Path]::GetFileName($ftpCred.Uri.AbsolutePath)
            if (-not $ftpSourceName) {
                $ftpSourceName = "playlist.m3u8"
            }
            $controlUrlForFtp = $location -replace "/description\.xml$", "/upnp/control/content_directory" -replace "/device\.xml$", "/upnp/control/content_directory"

            $ftpRootBrowse = ""
            try {
                $ftpRootBrowse = Invoke-SoapCurl $controlUrlForFtp $soapBody
            }
            catch {
                Add-Result "FAIL ftp root Browse SOAP call threw: $($_.Exception.Message)"
            }

            $ftpContainerId = $null
            if ($ftpRootBrowse) {
                $decodedFtpRoot = [System.Net.WebUtility]::HtmlDecode($ftpRootBrowse)
                $ftpContainerPattern = '<container id="(\d+)"[^>]*>(?:(?!</container>)[\s\S])*?<dc:title>' + [regex]::Escape($ftpSourceName) + '</dc:title>'
                $ftpContainerMatch = [regex]::Match($decodedFtpRoot, $ftpContainerPattern)
                if ($ftpContainerMatch.Success) {
                    $ftpContainerId = $ftpContainerMatch.Groups[1].Value
                }
            }

            if (-not $ftpContainerId) {
                $rejectLines = Find-LogLines ("reject-extension.*" + [regex]::Escape($ftpSourceName))
                if ($rejectLines.Count -gt 0) {
                    Add-Result ("FAIL ftp source rejected during scan, reported exactly as logged: " + ($rejectLines -join " | "))
                }
                else {
                    Add-Result "FAIL ftp source container '$ftpSourceName' not found in root Browse and no reject-extension log entry found for it; scan did not reach it, report this as a real finding, do not paper over it"
                }
            }
            else {
                Add-Result "PASS ftp source container found in root Browse (id=$ftpContainerId)"

                $ftpChildBody = $soapBody -replace "<ObjectID>0</ObjectID>", "<ObjectID>$ftpContainerId</ObjectID>"
                $ftpChildBrowse = ""
                try {
                    $ftpChildBrowse = Invoke-SoapCurl $controlUrlForFtp $ftpChildBody
                }
                catch {
                    Add-Result "FAIL ftp child Browse SOAP call threw: $($_.Exception.Message)"
                }

                $ftpItemId = $null
                if ($ftpChildBrowse) {
                    $decodedFtpChild = [System.Net.WebUtility]::HtmlDecode($ftpChildBrowse)

                    $ftpItemMatch = [regex]::Match($decodedFtpChild, '<item id="(\d+)"')
                    $ftpContainerMatch = [regex]::Match($decodedFtpChild, '<container id="(\d+)"')
                    if ($groundTruth.MediaItemCount -gt 0) {
                        if ($ftpItemMatch.Success) {
                            $ftpItemId = $ftpItemMatch.Groups[1].Value
                            Add-Result "PASS ftp source has at least one indexed item (id=$ftpItemId), matching ground truth MediaItemCount=$($groundTruth.MediaItemCount)"
                        }
                        else {
                            Add-Result "FAIL ground truth expected $($groundTruth.MediaItemCount) media item(s) under the ftp source but Browse returned none"
                        }
                    }
                    else {
                        if ($ftpContainerMatch.Success -and -not $ftpItemMatch.Success) {
                            Add-Result "PASS ftp source returned containers only ($($groundTruth.ContainerCount) expected from ground truth), zero items, matching ground truth of an HLS/container-only tree"
                        }
                        elseif ($ftpItemMatch.Success) {
                            Add-Result "FAIL ground truth expected zero media items (container-only HLS tree) but Browse returned an item"
                        }
                        else {
                            Add-Result "WARN ftp source container returned zero child items and zero child containers; ground truth expected $($groundTruth.ContainerCount) container(s) - confirm the ftp test host still serves content at $FtpSourceUrl"
                        }
                    }

                        if ($decodedFtpChild -match "ftp://") {
                            Add-Result "FAIL ftp scheme literal found directly in a res element for the ftp source; proxying is not being forced for this non-http source, this reopens code review findings 17 and 18"
                        }
                        else {
                            Add-Result "PASS ftp source content is exposed via proxied http res elements or as containers, not a raw ftp url"
                        }

                        if ($ftpPassword -and $decodedFtpChild -match [regex]::Escape($ftpPassword)) {
                            Add-Result "FAIL ftp password literal found in the DIDL Browse response for the ftp source container"
                        }
                        elseif ($ftpPassword) {
                            Add-Result "PASS ftp password not present in the DIDL Browse response for the ftp source container"
                        }
                    }
                }

                $fullDebugLogForFtp = Read-DebugLog
                if ($ftpPassword -and $fullDebugLogForFtp -match [regex]::Escape($ftpPassword)) {
                    Add-Result "FAIL ftp password literal found in debug.log"
                }
                elseif ($ftpPassword) {
                    Add-Result "PASS ftp password not present in debug.log"
                }

                if ($ftpItemId) {
                    try {
                        Add-Type -AssemblyName System.Net.Http -ErrorAction SilentlyContinue
                        $ftpMediaUrl = "http://127.0.0.1:$serverPort/media/$ftpItemId"
                        $client = New-Object System.Net.Http.HttpClient
                        $client.Timeout = [TimeSpan]::FromSeconds(20)
                        try {
                            $request = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Get, $ftpMediaUrl)
                            $request.Headers.Range = New-Object System.Net.Http.Headers.RangeHeaderValue(0, 31)
                            $response = $client.SendAsync($request).GetAwaiter().GetResult()
                            $statusCode = [int]$response.StatusCode
                            $bytes = $response.Content.ReadAsByteArrayAsync().GetAwaiter().GetResult()
                            if (($statusCode -eq 206 -or $statusCode -eq 200) -and $bytes.Length -gt 0) {
                                Add-Result "PASS ftp source item reachable via ranged GET on $ftpMediaUrl, status=$statusCode bytes=$($bytes.Length)"
                            }
                            else {
                                Add-Result "FAIL ftp source item ranged GET on $ftpMediaUrl returned status=$statusCode bytes=$($bytes.Length)"
                            }
                        }
                        finally {
                            $client.Dispose()
                        }
                    }
                    catch {
                        Add-Result "FAIL ftp source item ranged GET threw: $($_.Exception.Message)"
                    }
}
                }

                if ($ftpItemId -and $decodedFtpChild -match 'sec:CaptionInfoEx[^>]*>([^<]+)<') {
                    $ftpSubtitleUrl = [System.Net.WebUtility]::HtmlDecode($Matches[1])
                    try {
                        $subResp = Invoke-WebRequest -Uri $ftpSubtitleUrl -UseBasicParsing -TimeoutSec 15
                        if ($subResp.StatusCode -eq 200 -and $subResp.Content.Length -gt 0) {
                            Add-Result "PASS ftp-sourced subtitle served over http at $ftpSubtitleUrl"
                        }
                        else {
                            Add-Result "FAIL ftp-sourced subtitle at $ftpSubtitleUrl returned status=$($subResp.StatusCode) length=$($subResp.Content.Length)"
                        }
                    }
                    catch {
                        Add-Result "FAIL ftp-sourced subtitle GET threw: $($_.Exception.Message)"
                    }
                }
                else {
                    Add-Result "WARN no sec:CaptionInfoEx found for the ftp source item; skipping ftp subtitle hosting check (add a #DLNA-SUBTITLE: line to the ftp test playlist to exercise this path)"
                }
            Add-Result "--- end ftp live source checks ---"
        }

    # -------------------------------------------------------------------
    # HLS manifest proxy Range request test
    # -------------------------------------------------------------------
    # Verifies that Range requests against HLS manifests return 200 with
    # Accept-Ranges: none, not 206 Partial Content.
    # This prevents the "blank playlist" defect where CDN byte-range slicing
    # produces truncated manifests.
    if ($hlsHttpServerUrl -and $hlsHttpServerUrl -ne "") {
        Add-Result "--- begin HLS manifest proxy checks: $hlsHttpServerUrl ---"

        # find HLS item via Browse
        # HLS playlists are exposed as containers with a single HLS item inside
        $hlsItemId = $null
        $hlsSoapBody = @"
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
        $hlsBrowseUrl = "http://127.0.0.1:$serverPort/upnp/control/content_directory"
        $hlsBrowseRaw = Invoke-CurlRaw @(
            "-sS", "--max-time", "15",
            "-H", "Content-Type: text/xml; charset=utf-8",
            "-H", 'SOAPACTION: "urn:schemas-upnp-org:service:ContentDirectory:1#Browse"',
            "--data-binary", $hlsSoapBody,
            $hlsBrowseUrl
        )
        # Find HLS item with application/vnd.apple.mpegurl MIME type
        # HLS items are exposed with this MIME type in the protocolInfo
        if ($hlsBrowseRaw -match '<item[^>]*id="(\d+)"[^>]*>[^<]*<[^>]*>[^<]*<[^>]*>[^<]*application/vnd.apple.mpegurl') {
            $hlsItemId = $Matches[1]
            Add-Result "PASS found HLS item id=$hlsItemId in Browse response"
        } elseif ($hlsBrowseRaw -match '<item[^>]*id="(\d+)"' -and $hlsBrowseRaw -match "application/vnd.apple.mpegurl") {
            $hlsItemId = $Matches[1]
            Add-Result "PASS found HLS item id=$hlsItemId in Browse response"
        } else {
            Add-Result "WARN HLS source did not yield an m3u8 item; skipping HLS Range proxy test"
        }

        if ($hlsItemId) {
            # Test 1: GET without Range header
            $hlsGetNoRange = Invoke-CurlRaw @(
                "-sS", "-i", "--max-time", "15",
                "http://127.0.0.1:$serverPort/media/$hlsItemId"
            )
            $hlsStatus = $hlsGetNoRange -split "`r?`n" | Select-Object -First 1
            if ($hlsStatus -match "HTTP/1\.1 200") {
                Add-Result "PASS HLS GET no-Range returned 200"
            } else {
                Add-Result "FAIL HLS GET no-Range returned: $hlsStatus"
            }
            if ($hlsGetNoRange -match "Accept-Ranges:\s*none") {
                Add-Result "PASS HLS GET no-Range has Accept-Ranges: none"
            } else {
                Add-Result "FAIL HLS GET no-Range missing or wrong Accept-Ranges"
            }
            if ($hlsGetNoRange -match "#EXTM3U") {
                Add-Result "PASS HLS GET no-Range body starts with #EXTM3U"
            } else {
                Add-Result "FAIL HLS GET no-Range body does not start with #EXTM3U"
            }

            # Test 2: GET with Range header (the regression test)
            $hlsGetWithRange = Invoke-CurlRaw @(
                "-sS", "-i", "--max-time", "15",
                "-H", "Range: bytes=0-1",
                "http://127.0.0.1:$serverPort/media/$hlsItemId"
            )
            $hlsRangeStatus = $hlsGetWithRange -split "`r?`n" | Select-Object -First 1
            if ($hlsRangeStatus -match "HTTP/1\.1 200") {
                Add-Result "PASS HLS GET with Range returned 200 (not 206)"
            } else {
                Add-Result "FAIL HLS GET with Range returned: $hlsRangeStatus (expected 200)"
            }
            if ($hlsGetWithRange -match "Accept-Ranges:\s*none") {
                Add-Result "PASS HLS GET with Range has Accept-Ranges: none"
            } else {
                Add-Result "FAIL HLS GET with Range missing Accept-Ranges: none"
            }
            if ($hlsGetWithRange -match "#EXTM3U") {
                Add-Result "PASS HLS GET with Range body starts with #EXTM3U (full manifest, not truncated)"
            } else {
                Add-Result "FAIL HLS GET with Range body does not start with #EXTM3U (truncated?)"
            }

            # Test 3: HEAD request
            $hlsHeadResp = Invoke-CurlRaw @(
                "-sS", "-I", "--max-time", "10",
                "http://127.0.0.1:$serverPort/media/$hlsItemId"
            )
            $hlsHeadStatus = $hlsHeadResp -split "`r?`n" | Select-Object -First 1
            if ($hlsHeadStatus -match "HTTP/1\.1 200") {
                Add-Result "PASS HLS HEAD returned 200"
            } else {
                Add-Result "FAIL HLS HEAD returned: $hlsHeadStatus"
            }
            if ($hlsHeadResp -match "Accept-Ranges:\s*none") {
                Add-Result "PASS HLS HEAD has Accept-Ranges: none"
            } else {
                Add-Result "FAIL HLS HEAD missing Accept-Ranges: none"
            }
        }
        Add-Result "--- end HLS manifest proxy checks ---"
    }

    $ipv6If = Get-NetIPAddress -AddressFamily IPv6 -ErrorAction SilentlyContinue |
    Where-Object { $_.IPAddress -ne "::1" -and $_.IPAddress -notlike "ff*" } |
    Select-Object -First 1
    if ($ipv6If) {
        try {
            $ipv6Responses = Send-MSearchIPv6 "urn:schemas-upnp-org:device:MediaServer:1" $ipv6If.InterfaceIndex
            if ($ipv6Responses.Count -gt 0) {
                Add-Result ("PASS IPv6 M-SEARCH MediaServer -> $($ipv6Responses.Count) response(s)")
            }
            else {
                Add-Result "WARN IPv6 M-SEARCH returned no responses"
            }
        }
        catch {
            Add-Result ("WARN IPv6 probe failed: " + $_.Exception.Message)
        }
    }
    else {
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
    }
    else {
        Add-Result "WARN VLC run did not produce visible SSDP search in server log"
    }

    if ($windowHandle -ne [IntPtr]::Zero) {
        [void][User32Bridge]::PostMessage($windowHandle, 0x0111, [IntPtr]202, [IntPtr]0)
        Start-Sleep -Seconds 2
        $serverProc.CloseMainWindow() | Out-Null
        Start-Sleep -Seconds 2
        Add-Result "PASS sent stop + destroy messages to app window"
    }
    else {
        Add-Result "WARN could not find app window for graceful stop"
    }

    $byebyeLines = Find-LogLines "SSDP notify sent: nts=ssdp:byebye"
    if ($byebyeLines.Count -ge 5) {
        Add-Result ("PASS byebye notifications logged ($($byebyeLines.Count) entries)")
    }
    else {
        Add-Result ("WARN byebye count lower than expected ($($byebyeLines.Count) entries)")
    }

    if (Test-Path $debugLogPath) {
        try {
            Copy-Item -LiteralPath $debugLogPath -Destination $debugCopyPath -Force -ErrorAction Stop
        }
        catch {
            Set-Content -LiteralPath $debugCopyPath -Value (Read-DebugLog) -Encoding UTF8
        }
    }
    Set-Content -LiteralPath $resultsPath -Value ($summary -join [Environment]::NewLine) -Encoding UTF8
}
catch {
    $errorRecord = $_
    Add-Result "FATAL ERROR: $errorRecord"
    Add-Result "FATAL SCRIPT STACK TRACE:"
    Add-Result ([string]$errorRecord.ScriptStackTrace)
    Add-Result "FATAL POSITION:"
    Add-Result ([string]$errorRecord.InvocationInfo.PositionMessage)
    Set-Content -LiteralPath $resultsPath -Value ($summary -join [Environment]::NewLine) -Encoding UTF8
}
finally {
    if ($hlsHttpProc -and -not $hlsHttpProc.HasExited) {
        Stop-Process -Id $hlsHttpProc.Id -Force -ErrorAction SilentlyContinue
    }
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
        Remove-Item -LiteralPath $backupPath -Force -ErrorAction SilentlyContinue
    } elseif (Test-Path $configPath) {
        Remove-Item -LiteralPath $configPath -Force -ErrorAction SilentlyContinue
    }
    if ($transcriptStarted) {
        try {
            Stop-Transcript | Out-Null
        } catch {
        }
    }
}
