param(
    [string]$FtpSourceUrl = "ftp://14ag:qwertyui@192.168.100.33:2121/playlist_remote.m3u8"
)

$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$outputDir = Join-Path $repo "output\winx64"
$exePath = Join-Path $outputDir "DLNA Server.exe"
if (-not (Test-Path -LiteralPath $exePath)) {
    throw "Missing build binary at $exePath. Build first."
}

$configPath = Join-Path $outputDir "config.ini"
$debugLogPath = Join-Path $outputDir "debug.log"
$serverPort = 18201
$serverProc = $null
$results = New-Object System.Collections.Generic.List[string]

function Add-Result { param([string]$line) $results.Add($line) | Out-Null }

# Resolve the FTP source name once, up front, so every check that needs it
# (log-line matching, DIDL container-title matching) uses the same value
# derived from the actual -FtpSourceUrl parameter instead of a hardcoded
# literal that silently stops matching if the parameter is ever changed.
function Get-FtpUrlCredential {
    param([string]$Url)
    try { $uri = [System.Uri]$Url } catch { return $null }
    if (-not $uri.UserInfo) { return $null }
    $parts = $uri.UserInfo.Split(":", 2)
    $password = ""
    if ($parts.Count -gt 1) { $password = [System.Uri]::UnescapeDataString($parts[1]) }
    return [PSCustomObject]@{
        Uri      = $uri
        User     = [System.Uri]::UnescapeDataString($parts[0])
        Password = $password
    }
}

$script:ftpCredForName = Get-FtpUrlCredential -Url $FtpSourceUrl
$script:ftpSourceName = if ($script:ftpCredForName) {
    [System.IO.Path]::GetFileName($script:ftpCredForName.Uri.AbsolutePath)
} else {
    [System.IO.Path]::GetFileName(([System.Uri]$FtpSourceUrl).AbsolutePath)
}
if (-not $script:ftpSourceName) { $script:ftpSourceName = "playlist_remote.m3u8" }

# --- helpers (minimal, no smoke dependency) ---

function Set-IniValue {
    param([string]$section, [string]$key, [string]$value)
    if (Test-Path $configPath) {
        $lines = @(Get-Content -LiteralPath $configPath -Encoding UTF8)
    } else {
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
    foreach ($line in $lines) { $list.Add($line) }
    $list.Insert($insertIndex, "$key=$value")
    Set-Content -LiteralPath $configPath -Value $list -Encoding UTF8
}

function Read-DebugLog {
    if (-not (Test-Path $debugLogPath)) { return "" }
    Get-Content -LiteralPath $debugLogPath -Encoding UTF8 -Raw -ErrorAction SilentlyContinue
}

function Wait-PortReleased {
    param([int]$Port, [int]$TimeoutSeconds = 15)
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $listeners = Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue
        if (-not $listeners) { return $true }
        Start-Sleep -Milliseconds 300
    }
    return $false
}

# HARDENED: always returns @() rather than $null or a bare scalar, so every
# call site's .Count and [-1] indexing is well-defined regardless of match
# count (0, 1, or many).
function Find-LogLines {
    param([string]$pattern)
    $log = Read-DebugLog
    if (-not $log) { return @() }
    return @(($log -split '\r?\n') -match $pattern)
}

# FIX (primary crash cause): native command output captured via 2>&1 is a
# string[] when the response is multi-line (SOAP/DIDL/XML responses from
# this server always are). Joining to a single string before any -match /
# regex use is required, matching the pattern already used elsewhere in
# this repository (tests/verify-smoke.ps1 Invoke-CurlText/Invoke-CurlRaw).
function Invoke-NativeText {
    param([string]$FilePath, [string[]]$ArgumentList)
    $output = & $FilePath @ArgumentList 2>&1
    $text = ($output | Out-String)
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath exited with code ${LASTEXITCODE}: $($text.Trim())"
    }
    return $text
}

function Invoke-SoapCurl {
    param([string]$url, [string]$soapBody)
    $tmpFile = Join-Path $env:TEMP "agentic-soap-$(Get-Random).tmp"
    try {
        Set-Content -LiteralPath $tmpFile -Value $soapBody -Encoding UTF8 -NoNewline
        return Invoke-NativeText -FilePath "curl.exe" -ArgumentList @(
            "-sS", "--max-time", "15",
            "-X", "POST",
            "-H", "Content-Type: text/xml; charset=utf-8",
            "-H", 'SOAPACTION: "urn:schemas-upnp-org:service:ContentDirectory:1#Browse"',
            "--data-binary", "@$tmpFile",
            $url
        )
    } finally {
        Remove-Item -LiteralPath $tmpFile -Force -ErrorAction SilentlyContinue
    }
}

function Invoke-HttpGet {
    param([string]$url)
    $text = Invoke-NativeText -FilePath "curl.exe" -ArgumentList @("-sS", "--max-time", "15", "-o", "NUL", "-w", "%{http_code}", $url)
    return $text.Trim()
}

function Invoke-HttpGetRange {
    param([string]$url, [int]$rangeStart, [int]$rangeEnd)
    $text = Invoke-NativeText -FilePath "curl.exe" -ArgumentList @("-sS", "--max-time", "15", "-o", "NUL", "-w", "%{http_code} %{size_download}", "-r", "$rangeStart-$rangeEnd", $url)
    return $text.Trim()
}

# --- main test ---

try {
    Add-Result "=== agentic FTP nested playlist access test ==="
    Add-Result "INFO FtpSourceUrl=$FtpSourceUrl port=$serverPort"

    $lingering = Get-Process -Name "DLNA Server" -ErrorAction SilentlyContinue
    foreach ($proc in $lingering) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        try { Wait-Process -Id $proc.Id -Timeout 10 -ErrorAction SilentlyContinue } catch {}
    }

    if (-not (Wait-PortReleased -Port $serverPort -TimeoutSeconds 15)) {
        $stale = Get-NetTCPConnection -State Listen -LocalPort $serverPort -ErrorAction SilentlyContinue
        $stalePids = ($stale | Select-Object -ExpandProperty OwningProcess -Unique) -join " "
        throw "port $serverPort is still held by pid(s) $stalePids after stop attempt"
    }

    if (Test-Path $configPath) { Remove-Item -LiteralPath $configPath -Force }
    if (Test-Path $debugLogPath) { Remove-Item -LiteralPath $debugLogPath -Force }

    Add-Result "PASS config cleaned"

    $env:DLNA_SERVER_SKIP_FIREWALL = "1"
    try {
        $serverProc = Start-Process -FilePath $exePath -ArgumentList "--headless", "--port", "$serverPort", "--name", "DLNA Agentic Test", "--uuid", "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee", "--source", $FtpSourceUrl, "--debug" -PassThru
    } finally {
        Remove-Item Env::DLNA_SERVER_SKIP_FIREWALL -ErrorAction SilentlyContinue
    }
    Add-Result "INFO server PID=$($serverProc.Id)"

    $ownerDeadline = (Get-Date).AddSeconds(15)
    $ownerConfirmed = $false
    while ((Get-Date) -lt $ownerDeadline) {
        $listeners = Get-NetTCPConnection -State Listen -LocalPort $serverPort -ErrorAction SilentlyContinue
        if ($listeners) {
            $others = $listeners | Where-Object { $_.OwningProcess -ne $serverProc.Id }
            if ($others) {
                $otherPids = ($others | Select-Object -ExpandProperty OwningProcess -Unique) -join " "
                throw "port $serverPort is also held by pid(s) $otherPids while our pid is $($serverProc.Id)"
            }
            $mine = $listeners | Where-Object { $_.OwningProcess -eq $serverProc.Id }
            if ($mine) { $ownerConfirmed = $true; break }
        }
        if ($serverProc.HasExited) {
            throw "server process exited before binding port $serverPort"
        }
        Start-Sleep -Milliseconds 300
    }
    if (-not $ownerConfirmed) {
        throw "server process $($serverProc.Id) never became the sole listener on port $serverPort within 15 seconds"
    }
    Add-Result "PASS port $serverPort owned exclusively by pid $($serverProc.Id)"

    $scanDeadline = [datetime]::UtcNow.AddSeconds(120)
    $scanDone = $false
    while ([datetime]::UtcNow -lt $scanDeadline) {
        $scanLines = Find-LogLines "Scanned \d+ media items\."
        if ($scanLines.Count -gt 0) {
            Add-Result ("PASS scan complete: " + ($scanLines[-1] -replace '^\s+',''))
            $scanDone = $true
            break
        }
        Start-Sleep -Milliseconds 500
    }
    if (-not $scanDone) {
        Add-Result "FAIL scan did not complete within 120s"
        throw "Scan timeout"
    }
    Start-Sleep -Seconds 2

    # FIX: pattern now derived from the actual configured source name
    # instead of the hardcoded literal "playlist_remote.m3u8".
    $ftpScanPattern = "Scanned \d+ media items from " + [regex]::Escape($script:ftpSourceName)
    $ftpScanLine = Find-LogLines $ftpScanPattern
    if ($ftpScanLine.Count -gt 0) {
        Add-Result ("PASS per-source FTP scan logged: " + ($ftpScanLine[-1] -replace '^\s+',''))
    } else {
        Add-Result "FAIL no per-source FTP scan count found"
    }

    # FIX (crash site): join multi-line curl output before -match, and read
    # matches from a local capture group instead of the shared $Matches
    # automatic variable, which is only populated for scalar -match input.
    $locFound = $false
    $location = $null
    for ($attempt = 1; $attempt -le 10; $attempt++) {
        $locationSearchText = (& curl.exe -sS --max-time 5 "http://127.0.0.1:$serverPort/description.xml" 2>&1 | Out-String)
        if ($LASTEXITCODE -eq 0) {
            $urlBaseMatch = [regex]::Match($locationSearchText, '<URLBase>([^<]+)</URLBase>')
            if ($urlBaseMatch.Success) {
                $location = $urlBaseMatch.Groups[1].Value
                $locFound = $true
                break
            }
        }
        Start-Sleep -Milliseconds 500
    }
    if (-not $locFound) {
        $localIPs = @("172.29.112.1", "192.168.100.163")
        foreach ($ip in $localIPs) {
            $locationSearchText = (& curl.exe -sS --max-time 5 "http://${ip}:$serverPort/description.xml" 2>&1 | Out-String)
            if ($LASTEXITCODE -eq 0) {
                $urlBaseMatch = [regex]::Match($locationSearchText, '<URLBase>([^<]+)</URLBase>')
                if ($urlBaseMatch.Success) {
                    $location = $urlBaseMatch.Groups[1].Value
                    $locFound = $true
                    break
                }
            }
        }
    }
    if (-not $locFound) {
        Add-Result "FAIL could not retrieve description.xml"
        throw "Discovery failed"
    }
    # URLBase from description.xml is host:port without scheme; construct full control URL
    $controlUrl = "http://$location/upnp/control/content_directory"
    Add-Result "PASS description.xml retrieved, controlUrl=$controlUrl"

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
    $rootBrowse = Invoke-SoapCurl $controlUrl $soapBody
    $decodedRoot = [System.Net.WebUtility]::HtmlDecode($rootBrowse)

    $ftpContainerPattern = '<container id="(\d+)"[^>]*>(?:(?!</container>)[\s\S])*?<dc:title>' + [regex]::Escape($script:ftpSourceName) + '</dc:title>'
    $ftpContainerMatch = [regex]::Match($decodedRoot, $ftpContainerPattern)
    if (-not $ftpContainerMatch.Success) {
        Add-Result "FAIL FTP playlist container '$($script:ftpSourceName)' not found in root Browse"
        throw "FTP container missing"
    }
    $ftpContainerId = $ftpContainerMatch.Groups[1].Value
    Add-Result "PASS FTP playlist container found in root Browse (id=$ftpContainerId)"

    $ftpChildBody = $soapBody -replace "<ObjectID>0</ObjectID>", "<ObjectID>$ftpContainerId</ObjectID>"
    $ftpChildBrowse = Invoke-SoapCurl $controlUrl $ftpChildBody
    $decodedFtpChild = [System.Net.WebUtility]::HtmlDecode($ftpChildBrowse)

    # A source manifest detected as HLS is exposed as a single leaf <item>
    # directly under this container (see AddHlsStreamItem in
    # src/media_sources.cpp / src/posix_media_sources.cpp) rather than as
    # nested variant <container> entries. Handle both shapes instead of
    # assuming nested containers always exist.
    $childContainers = [regex]::Matches($decodedFtpChild, 'container id="(\d+)"')
    $directResMatches = [regex]::Matches($decodedFtpChild, '<res[^>]*protocolInfo="[^"]*"[^>]*>([^<]+)</res>')

    $foundMediaId = $null
    $foundMediaUrl = $null

    if ($directResMatches.Count -gt 0) {
        $foundMediaUrl = $directResMatches[0].Groups[1].Value
        $itemIdMatch = [regex]::Match($decodedFtpChild, 'item id="(\d+)"')
        if ($itemIdMatch.Success) { $foundMediaId = $itemIdMatch.Groups[1].Value }
        Add-Result "PASS found media directly under FTP playlist container id=$ftpContainerId (single-manifest/HLS shape)"
    } elseif ($childContainers.Count -gt 0) {
        Add-Result "PASS found $($childContainers.Count) nested container(s) inside FTP playlist"
        $currentContainerId = $childContainers[0].Groups[1].Value
        $depth = 0
        $maxDepth = 10
        while ($depth -lt $maxDepth -and -not $foundMediaId) {
            $depth++
            $browseBody = $soapBody -replace "<ObjectID>0</ObjectID>", "<ObjectID>$currentContainerId</ObjectID>"
            $browseResult = Invoke-SoapCurl $controlUrl $browseBody
            $decodedResult = [System.Net.WebUtility]::HtmlDecode($browseResult)

            $resMatches = [regex]::Matches($decodedResult, '<res[^>]*protocolInfo="[^"]*"[^>]*>([^<]+)</res>')
            $nextContainers = [regex]::Matches($decodedResult, 'container id="(\d+)"')

            if ($resMatches.Count -gt 0) {
                $foundMediaUrl = $resMatches[0].Groups[1].Value
                $itemIdMatch = [regex]::Match($decodedResult, 'item id="(\d+)"')
                if ($itemIdMatch.Success) { $foundMediaId = $itemIdMatch.Groups[1].Value }
                Add-Result "PASS found media at depth $depth in container id=$currentContainerId"
                Add-Result "INFO media URL: $foundMediaUrl"
                break
            }
            if ($nextContainers.Count -gt 0) {
                $currentContainerId = $nextContainers[0].Groups[1].Value
                Add-Result "INFO depth ${depth}: container id=$currentContainerId (descending further)"
                continue
            }
            Add-Result "FAIL depth ${depth}: container id=$currentContainerId empty (no res, no sub-containers)"
            break
        }
    } else {
        Add-Result "FAIL FTP playlist container id=$ftpContainerId has neither a direct media item nor nested containers"
    }

    if (-not $foundMediaId) {
        Add-Result "FAIL could not find any playable media item in FTP playlist tree"
        throw "No media found"
    }

    $mediaProxyUrl = "http://127.0.0.1:$serverPort/media/$foundMediaId"
    $getStatus = Invoke-HttpGet $mediaProxyUrl
    if ($getStatus -eq "200") {
        Add-Result "PASS media HTTP GET returned 200 at $mediaProxyUrl"
    } else {
        Add-Result "FAIL media HTTP GET returned status $getStatus at $mediaProxyUrl"
    }

    $rangeResult = Invoke-HttpGetRange $mediaProxyUrl 0 1023
    $rangeParts = $rangeResult -split ' '
    $rangeStatus = $rangeParts[0]
    $rangeSize = $rangeParts[1]
    if ($rangeStatus -match "20[06]" -and [int]$rangeSize -gt 0) {
        Add-Result "PASS media range GET returned $rangeStatus with $rangeSize bytes at $mediaProxyUrl"
    } else {
        Add-Result "WARN media range GET returned $rangeResult at $mediaProxyUrl"
    }

    $ftpCredMatch = [regex]::Match($FtpSourceUrl, 'ftp://([^:]+):([^@]+)@')
    if ($ftpCredMatch.Success) {
        $ftpPass = $ftpCredMatch.Groups[2].Value
        $fullLog = Read-DebugLog
        if ($fullLog -match [regex]::Escape($ftpPass)) {
            Add-Result "FAIL FTP password found in debug.log"
        } else {
            Add-Result "PASS FTP password absent from debug.log"
        }
    }

    Add-Result "=== test complete ==="
} catch {
    Add-Result "FATAL: $($_.Exception.Message)"
} finally {
    if ($serverProc -and -not $serverProc.HasExited) {
        $serverProc.Refresh()
        if (-not $serverProc.HasExited) {
            Stop-Process -Id $serverProc.Id -Force -ErrorAction SilentlyContinue
        }
    }
    $resultText = $results -join [Environment]::NewLine
    Write-Host $resultText
    $resultPath = Join-Path $outputDir "agentic-ftp-results.txt"
    Set-Content -LiteralPath $resultPath -Value $resultText -Encoding UTF8
    $failed = ($results | Where-Object { $_ -match "^FAIL" }).Count
    if ($failed -gt 0) {
        Write-Host "`n$failed FAIL(s) detected. See $resultPath"
        exit 1
    }
}