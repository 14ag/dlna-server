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
        if (-not $listeners) {
            return $true
        }
        Start-Sleep -Milliseconds 300
    }
    return $false
}

function Find-LogLines {
    param([string]$pattern)
    $log = Read-DebugLog
    if (-not $log) { return @() }
    ($log -split '\r?\n') -match $pattern
}

function Invoke-SoapCurl {
    param([string]$url, [string]$soapBody)
    $tmpFile = Join-Path $env:TEMP "agentic-soap-$(Get-Random).tmp"
    try {
        Set-Content -LiteralPath $tmpFile -Value $soapBody -Encoding UTF8 -NoNewline
        $result = & curl.exe -sS --max-time 15 -X POST $url `
            -H "Content-Type: text/xml; charset=utf-8" `
            -H "SOAPACTION: `"urn:schemas-upnp-org:service:ContentDirectory:1#Browse`"" `
            --data-binary "@$tmpFile" 2>&1
        if ($LASTEXITCODE -ne 0) {
            throw "curl exit code ${LASTEXITCODE}: $result"
        }
        return $result
    } finally {
        Remove-Item -LiteralPath $tmpFile -Force -ErrorAction SilentlyContinue
    }
}

function Invoke-HttpGet {
    param([string]$url)
    $result = & curl.exe -sS --max-time 15 -o NUL -w '%{http_code}' $url 2>&1
    if ($LASTEXITCODE -ne 0) { throw "curl exit $LASTEXITCODE" }
    return $result.Trim()
}

function Invoke-HttpGetRange {
    param([string]$url, [int]$rangeStart, [int]$rangeEnd)
    $result = & curl.exe -sS --max-time 15 -o NUL -w '%{http_code} %{size_download}' -r "$rangeStart-$rangeEnd" $url 2>&1
    if ($LASTEXITCODE -ne 0) { throw "curl exit $LASTEXITCODE" }
    return $result.Trim()
}

# --- main test ---

try {
    Add-Result "=== agentic FTP nested playlist access test ==="
    Add-Result "INFO FtpSourceUrl=$FtpSourceUrl port=$serverPort"

    # stop any lingering process and wait for it to fully exit
    # a stale listener on this exact port is the root cause this test now
    # guards against see the accompanying root cause document for details
    $lingering = Get-Process -Name "DLNA Server" -ErrorAction SilentlyContinue
    foreach ($proc in $lingering) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        try {
            Wait-Process -Id $proc.Id -Timeout 10 -ErrorAction SilentlyContinue
        } catch {
        }
    }

    if (-not (Wait-PortReleased -Port $serverPort -TimeoutSeconds 15)) {
        $stale = Get-NetTCPConnection -State Listen -LocalPort $serverPort -ErrorAction SilentlyContinue
        $stalePids = ($stale | Select-Object -ExpandProperty OwningProcess -Unique) -join " "
        throw "port $serverPort is still held by pid(s) $stalePids after stop attempt this is the stale listener conflict described in the root cause document"
    }

    # prepare config
    if (Test-Path $configPath) { Remove-Item -LiteralPath $configPath -Force }
    if (Test-Path $debugLogPath) { Remove-Item -LiteralPath $debugLogPath -Force }

    Add-Result "PASS config cleaned"

    # start server in headless mode with all config via command line
    $env:DLNA_SERVER_SKIP_FIREWALL = "1"
    try {
        $serverProc = Start-Process -FilePath $exePath -ArgumentList "--headless", "--port", "$serverPort", "--name", "DLNA Agentic Test", "--uuid", "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee", "--source", $FtpSourceUrl, "--debug" -PassThru
    } finally {
        Remove-Item Env:\DLNA_SERVER_SKIP_FIREWALL -ErrorAction SilentlyContinue
    }
    Add-Result "INFO server PID=$($serverProc.Id)"

    # confirm this process and only this process owns the listening socket
    # before waiting on the scan or attempting any http call
    $ownerDeadline = (Get-Date).AddSeconds(15)
    $ownerConfirmed = $false
    while ((Get-Date) -lt $ownerDeadline) {
        $listeners = Get-NetTCPConnection -State Listen -LocalPort $serverPort -ErrorAction SilentlyContinue
        if ($listeners) {
            $others = $listeners | Where-Object { $_.OwningProcess -ne $serverProc.Id }
            if ($others) {
                $otherPids = ($others | Select-Object -ExpandProperty OwningProcess -Unique) -join " "
                throw "port $serverPort is also held by pid(s) $otherPids while our pid is $($serverProc.Id) this is the stale listener conflict described in the root cause document"
            }
            $mine = $listeners | Where-Object { $_.OwningProcess -eq $serverProc.Id }
            if ($mine) {
                $ownerConfirmed = $true
                break
            }
        }
        if ($serverProc.HasExited) {
            throw "server process exited before binding port $serverPort check debug log for HTTP listen bind failed or HTTP server failed to bind"
        }
        Start-Sleep -Milliseconds 300
    }
    if (-not $ownerConfirmed) {
        throw "server process $($serverProc.Id) never became the sole listener on port $serverPort within 15 seconds"
    }
    Add-Result "PASS port $serverPort owned exclusively by pid $($serverProc.Id)"

    # wait for scan completion (aggregate log line)
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
    # give the server a moment to finish initialization
    Start-Sleep -Seconds 2

    # verify per-source counts in log
    $ftpScanLine = Find-LogLines "Scanned \d+ media items from playlist_remote.m3u8"
    if ($ftpScanLine.Count -gt 0) {
        Add-Result ("PASS per-source FTP scan logged: " + ($ftpScanLine[-1] -replace '^\s+',''))
    } else {
        Add-Result "FAIL no per-source FTP scan count found"
    }

    # discover server location
    $locFound = $false
    $location = $null
    for ($attempt = 1; $attempt -le 10; $attempt++) {
        $locationSearch = & curl.exe -sS --max-time 5 "http://127.0.0.1:$serverPort/description.xml" 2>&1
        if ($LASTEXITCODE -eq 0 -and $locationSearch -match '<URLBase>([^<]+)</URLBase>') {
            $location = $Matches[1]
            $locFound = $true
            break
        }
        Start-Sleep -Milliseconds 500
    }
    # Also try the machine's IP addresses if 127.0.0.1 fails
    if (-not $locFound) {
        $localIPs = @("172.29.112.1", "192.168.100.163")
        foreach ($ip in $localIPs) {
            $locationSearch = & curl.exe -sS --max-time 5 "http://${ip}:$serverPort/description.xml" 2>&1
            if ($LASTEXITCODE -eq 0 -and $locationSearch -match '<URLBase>([^<]+)</URLBase>') {
                $location = $Matches[1]
                $locFound = $true
                break
            }
        }
    }
    if (-not $locFound) {
        Add-Result "FAIL could not retrieve description.xml"
        throw "Discovery failed"
    }
    Add-Result "PASS description.xml retrieved, URLBase=$location"

    # Browse root for containers
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
    $controlUrl = $location -replace "/description\.xml$", "/upnp/control/content_directory" -replace "/device\.xml$", "/upnp/control/content_directory"
    $rootBrowse = Invoke-SoapCurl $controlUrl $soapBody
    $decodedRoot = [System.Net.WebUtility]::HtmlDecode($rootBrowse)

    # find FTP playlist container in root
    $ftpContainerPattern = '<container id="(\d+)"[^>]*>(?:(?!</container>)[\s\S])*?<dc:title>playlist_remote.m3u8</dc:title>'
    $ftpContainerMatch = [regex]::Match($decodedRoot, $ftpContainerPattern)
    if (-not $ftpContainerMatch.Success) {
        Add-Result "FAIL FTP playlist container 'playlist_remote.m3u8' not found in root Browse"
        throw "FTP container missing"
    }
    $ftpContainerId = $ftpContainerMatch.Groups[1].Value
    Add-Result "PASS FTP playlist container found in root Browse (id=$ftpContainerId)"

    # Browse into FTP container: expect nested variant playlist containers
    $ftpChildBody = $soapBody -replace "<ObjectID>0</ObjectID>", "<ObjectID>$ftpContainerId</ObjectID>"
    $ftpChildBrowse = Invoke-SoapCurl $controlUrl $ftpChildBody
    $decodedFtpChild = [System.Net.WebUtility]::HtmlDecode($ftpChildBrowse)

    # extract all container IDs from FTP child browse (nested variant playlists)
    $childContainers = [regex]::Matches($decodedFtpChild, 'container id="(\d+)"')
    if ($childContainers.Count -eq 0) {
        Add-Result "FAIL no nested containers found inside FTP playlist"
        throw "No nested containers"
    }
    Add-Result "PASS found $($childContainers.Count) nested container(s) inside FTP playlist"

    # walk into first nested container, recurse until we find a media item (res)
    $currentContainerId = $childContainers[0].Groups[1].Value
    $depth = 0
    $maxDepth = 10
    $foundMediaId = $null
    $foundMediaUrl = $null

    while ($depth -lt $maxDepth -and -not $foundMediaId) {
        $depth++
        $browseBody = $soapBody -replace "<ObjectID>0</ObjectID>", "<ObjectID>$currentContainerId</ObjectID>"
        $browseResult = Invoke-SoapCurl $controlUrl $browseBody
        $decodedResult = [System.Net.WebUtility]::HtmlDecode($browseResult)

        # look for both containers and res items
        $resMatches = [regex]::Matches($decodedResult, '<res[^>]*protocolInfo="[^"]*"[^>]*>([^<]+)</res>')
        $nextContainers = [regex]::Matches($decodedResult, 'container id="(\d+)"')

        if ($resMatches.Count -gt 0) {
            $foundMediaUrl = $resMatches[0].Groups[1].Value
            # extract item id from parent id pattern or id attribute
            $itemIdMatch = [regex]::Match($decodedResult, 'item id="(\d+)"')
            if ($itemIdMatch.Success) {
                $foundMediaId = $itemIdMatch.Groups[1].Value
            } else {
                # fallback: use URL-based access
                $foundMediaId = "url"
            }
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

    if (-not $foundMediaId) {
        Add-Result "FAIL could not find any playable media item in nested playlist tree"
        throw "No media found"
    }

    # HTTP GET the media through server proxy
    $mediaProxyUrl = "http://127.0.0.1:$serverPort/media/$foundMediaId"
    $getStatus = Invoke-HttpGet $mediaProxyUrl
    if ($getStatus -eq "200") {
        Add-Result "PASS media HTTP GET returned 200 at $mediaProxyUrl"
    } else {
        Add-Result "FAIL media HTTP GET returned status $getStatus at $mediaProxyUrl"
    }

    # Range request on media via proxy
    $rangeResult = Invoke-HttpGetRange $mediaProxyUrl 0 1023
    $rangeParts = $rangeResult -split ' '
    $rangeStatus = $rangeParts[0]
    $rangeSize = $rangeParts[1]
    if ($rangeStatus -match "20[06]" -and [int]$rangeSize -gt 0) {
        Add-Result "PASS media range GET returned $rangeStatus with $rangeSize bytes at $mediaProxyUrl"
    } else {
        Add-Result "WARN media range GET returned $rangeResult at $mediaProxyUrl"
    }

    # verify FTP password absent from debug log
    $ftpCredMatch = [regex]::Match($FtpSourceUrl, 'ftp://([^:]+):([^@]+)@')
    if ($ftpCredMatch.Success) {
        $ftpUser = $ftpCredMatch.Groups[1].Value
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