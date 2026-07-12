#Requires -Version 5.1

<#
.SYNOPSIS
    HLS nested playlist structure parity smoke test.
.DESCRIPTION
    Replicates the manual traversal procedure used to produce
    hls_access_comparison_report.txt and hls-playlist-findings.txt, as a
    repeatable, pass/fail automated check.

    Traverses the HLS tree two independent ways using curl.exe:
      1. Direct: local wrapper file (file://) -> upstream CDN master ->
         each variant/media child playlist.
      2. Server: dlna-server ContentDirectory item -> /media/<id> master ->
         each child playlist referenced in that master's rewritten URIs.

    Volatile per-fetch fields (EXT-X-MEDIA-SEQUENCE value,
    EXT-X-PROGRAM-DATE-TIME value, segment filenames/timestamps) are not
    compared, since HLS is a live rolling window and two curl calls made
    seconds apart will never share exact segment names. Tag composition,
    EXT-X-STREAM-INF attributes (bandwidth normalized), child URI sets,
    and per-child segment/EXTINF counts are compared.

    The naive server-relative child route (http://<server>/media/<child
    filename>) is separately asserted to still return 400. That route is
    expected to remain unreachable by design: after the fix, clients never
    construct it, since the served master already contains absolute
    origin URLs.
.PARAMETER ServerBase
    Base URL of the running dlna-server instance.
.PARAMETER LocalWrapperPath
    Path to the local .m3u8 wrapper file that references the upstream HLS
    master, relative to the current working directory.
.PARAMETER ItemTitle
    DIDL title to look for while browsing the ContentDirectory root,
    matches the wrapper file name by default.
.PARAMETER ServerRootUrl
    Skips ContentDirectory discovery and uses this resource URL directly,
    for example http://localhost:8200/media/1000010. Use this if
    discovery fails to parse your server's DIDL-Lite response shape.
.EXAMPLE
    .\hls-structure-smoke.ps1
.EXAMPLE
    .\hls-structure-smoke.ps1 -ServerRootUrl 'http://localhost:8200/media/1000010'
#>

[CmdletBinding()]
param(
    [string]$ServerBase = 'http://localhost:8200',
    [string]$LocalWrapperPath = '.\test media\test-hls-playlist.m3u8',
    [string]$ItemTitle = 'test-hls-playlist.m3u8',
    [string]$ServerRootUrl
)

$script:ExitCode = 0
$script:Failures = [System.Collections.Generic.List[string]]::new()

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


function Write-Section {
    param([Parameter(Mandatory)][string]$Text)
    Write-Host "`n== $Text ==" -ForegroundColor Cyan
}

function Write-Pass {
    param([Parameter(Mandatory)][string]$Text)
    Write-Host "[PASS] $Text" -ForegroundColor Green
}

function Write-Fail {
    param([Parameter(Mandatory)][string]$Text)
    Write-Host "[FAIL] $Text" -ForegroundColor Red
    $script:Failures.Add($Text)
    $script:ExitCode = 1
}

function Invoke-Curl {
    param([Parameter(Mandatory)][string]$Url)
    $raw = & curl.exe -sS -m 15 -w "`n%{http_code}" $Url 2>$null
    if (-not $raw) { return [pscustomobject]@{ StatusCode = 0; Body = '' } }
    if ($raw -is [string]) { $raw = @($raw) }
    $statusCode = 0
    [void][int]::TryParse($raw[-1].Trim(), [ref]$statusCode)
    $body = if ($raw.Count -gt 1) { ($raw[0..($raw.Count - 2)]) -join "`n" } else { '' }
    [pscustomobject]@{ StatusCode = $statusCode; Body = $body }
}

function Get-DidlNodes {
    param([Parameter(Mandatory)][xml]$DidlXml)
    $ns = New-Object System.Xml.XmlNamespaceManager($DidlXml.NameTable)
    $ns.AddNamespace('didl', 'urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/')
    $ns.AddNamespace('dc', 'http://purl.org/dc/elements/1.1/')
    $result = @()
    foreach ($node in $DidlXml.SelectNodes('//didl:container | //didl:item', $ns)) {
        $titleNode = $node.SelectSingleNode('dc:title', $ns)
        $resNode = $node.SelectSingleNode('didl:res', $ns)
        $result += [pscustomobject]@{
            Id    = $node.GetAttribute('id')
            Title = if ($titleNode) { $titleNode.InnerText } else { '' }
            Type  = $node.LocalName
            Res   = if ($resNode) { $resNode.InnerText } else { $null }
        }
    }
    return $result
}

function Get-CdsChildren {
    param([Parameter(Mandatory)][string]$ObjectId)
    $soapBody = @"
<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
  <s:Body>
    <u:Browse xmlns:u="urn:schemas-upnp-org:service:ContentDirectory:1">
      <ObjectID>$ObjectId</ObjectID>
      <BrowseFlag>BrowseDirectChildren</BrowseFlag>
      <Filter>*</Filter>
      <StartingIndex>0</StartingIndex>
      <RequestedCount>0</RequestedCount>
      <SortCriteria></SortCriteria>
    </u:Browse>
  </s:Body>
</s:Envelope>
"@
    $headers = @{ SOAPACTION = '"urn:schemas-upnp-org:service:ContentDirectory:1#Browse"' }
    $response = Invoke-WebRequest -Uri "$ServerBase/upnp/control/content_directory" -Method Post `
        -Body $soapBody -ContentType 'text/xml; charset="utf-8"' -Headers $headers -UseBasicParsing
    [xml]$envelope = $response.Content
    [xml]$didl = $envelope.Envelope.Body.BrowseResponse.Result
    return Get-DidlNodes -DidlXml $didl
}

function Find-HlsResourceUrl {
    param([Parameter(Mandatory)][string]$Title)
    $root = Get-CdsChildren -ObjectId '0'
    $container = $root | Where-Object { $_.Type -eq 'container' -and $_.Title -eq $Title } | Select-Object -First 1
    if (-not $container) {
        throw "No container titled '$Title' found under ContentDirectory root. Pass -ServerRootUrl to skip discovery."
    }
    $children = Get-CdsChildren -ObjectId $container.Id
    $item = $children | Where-Object { $_.Type -eq 'item' -and $_.Res } | Select-Object -First 1
    if (-not $item) {
        throw "No playable item with a res URL found under container '$Title'. Pass -ServerRootUrl to skip discovery."
    }
    return $item.Res
}

function Resolve-HlsUri {
    param([Parameter(Mandatory)][string]$BaseUrl, [Parameter(Mandatory)][string]$RelativeOrAbsolute)
    return [Uri]::new([Uri]$BaseUrl, $RelativeOrAbsolute).AbsoluteUri
}

function ConvertTo-NormalizedHlsStructure {
    param([Parameter(Mandatory)][AllowEmptyString()][string]$ManifestText, [Parameter(Mandatory)][string]$ManifestUrl)

    $tags = [System.Collections.Generic.List[string]]::new()
    $children = [System.Collections.Generic.List[string]]::new()
    $extinfCount = 0

    foreach ($rawLine in ($ManifestText -split "`r`n|`n")) {
        $line = $rawLine.Trim()
        if ([string]::IsNullOrEmpty($line)) { continue }

        if ($line[0] -ne '#') {
            $children.Add((Resolve-HlsUri -BaseUrl $ManifestUrl -RelativeOrAbsolute $line))
            continue
        }

        $tagName = ($line -split '[:,]', 2)[0].TrimStart('#')
        if ($tagName -eq 'EXTINF') { $extinfCount++ }

        if ($tagName -eq 'EXT-X-STREAM-INF') {
            $attrs = ($line -replace '^#EXT-X-STREAM-INF:', '') -replace 'BANDWIDTH=\d+', 'BANDWIDTH=N'
            $tags.Add("EXT-X-STREAM-INF($attrs)")
        } else {
            $tags.Add($tagName)
        }

        if ($line -match 'URI="([^"]*)"' -and $Matches[1]) {
            $children.Add((Resolve-HlsUri -BaseUrl $ManifestUrl -RelativeOrAbsolute $Matches[1]))
        }
    }

    [pscustomobject]@{
        Tags        = $tags
        Children    = $children
        ExtinfCount = $extinfCount
    }
}

# --- discovery ---
$configPath = '..\output\winx64\config.ini'
$mediaSourcesValue = 'C:\Users\philip\sauce\dlna-server\tests\test media\test-hls-playlist.m3u8'
$exePath='..\output\winx64\DLNA Server.exe'
Set-IniValue "Settings" "MediaSources" $mediaSourcesValue
Start-Process -FilePath $exePath -PassThru -ArgumentList '-h'
Start-Sleep -Milliseconds 500
Start-Sleep -Milliseconds 500


Write-Section 'Discovering server resource'
if (-not $ServerRootUrl) {
    try {
        $ServerRootUrl = Find-HlsResourceUrl -Title $ItemTitle
        Write-Host "Resolved server resource: $ServerRootUrl"
    } catch {
        Write-Fail "ContentDirectory discovery failed: $($_.Exception.Message)"
        exit $script:ExitCode
    }
} else {
    Write-Host "Using supplied server resource: $ServerRootUrl"
}

# --- direct traversal ---

Write-Section 'Direct traversal (local wrapper -> upstream CDN master)'
if (-not (Test-Path -LiteralPath $LocalWrapperPath)) {
    Write-Fail "Local wrapper file not found: $LocalWrapperPath"
    exit $script:ExitCode
}
$wrapperFileUri = ([Uri]((Resolve-Path -LiteralPath $LocalWrapperPath).Path)).AbsoluteUri
$wrapperResult = Invoke-Curl -Url $wrapperFileUri
$upstreamMasterUrl = ($wrapperResult.Body -split "`r`n|`n" |
    Where-Object { $_.Trim() -and $_.Trim()[0] -ne '#' } |
    Select-Object -First 1)
if (-not $upstreamMasterUrl) {
    Write-Fail "Could not extract an upstream master URL from $LocalWrapperPath"
    exit $script:ExitCode
}
$upstreamMasterUrl = $upstreamMasterUrl.Trim()

$directMasterResult = Invoke-Curl -Url $upstreamMasterUrl
if ($directMasterResult.StatusCode -ne 200) {
    Write-Fail "Direct upstream master fetch failed, status $($directMasterResult.StatusCode): $upstreamMasterUrl"
    exit $script:ExitCode
}
$directMaster = ConvertTo-NormalizedHlsStructure -ManifestText $directMasterResult.Body -ManifestUrl $upstreamMasterUrl
Write-Host "Direct master: $($directMaster.Children.Count) children"

# --- server traversal ---

Write-Section 'Server traversal (ContentDirectory item -> /media/ master)'
$serverMasterResult = Invoke-Curl -Url $ServerRootUrl
if ($serverMasterResult.StatusCode -ne 200) {
    Write-Fail "Server master fetch failed, status $($serverMasterResult.StatusCode): $ServerRootUrl"
    exit $script:ExitCode
}
$serverMaster = ConvertTo-NormalizedHlsStructure -ManifestText $serverMasterResult.Body -ManifestUrl $ServerRootUrl
Write-Host "Server master: $($serverMaster.Children.Count) children"

# --- master level comparison ---

Write-Section 'Master structure comparison'
$directTagsJoined = $directMaster.Tags -join '|'
$serverTagsJoined = $serverMaster.Tags -join '|'
if ($directTagsJoined -eq $serverTagsJoined) {
    Write-Pass 'Master tag structure identical'
} else {
    Write-Fail "Master tag structure differs`n  direct: $directTagsJoined`n  server: $serverTagsJoined"
}

$directChildrenSorted = $directMaster.Children | Sort-Object
$serverChildrenSorted = $serverMaster.Children | Sort-Object
$childDiff = Compare-Object -ReferenceObject $directChildrenSorted -DifferenceObject $serverChildrenSorted
if (-not $childDiff) {
    Write-Pass "Master child URI set identical ($($directChildrenSorted.Count) children)"
} else {
    Write-Fail "Master child URI set differs:`n$($childDiff | Format-Table -AutoSize | Out-String)"
}

# --- child level comparison ---

Write-Section 'Child playlist structure comparison'
$pairCount = [Math]::Min($directMaster.Children.Count, $serverMaster.Children.Count)
for ($i = 0; $i -lt $pairCount; $i++) {
    $directChildUrl = $directMaster.Children[$i]
    $serverChildUrl = $serverMaster.Children[$i]
    $label = Split-Path -Leaf ([Uri]$serverChildUrl).AbsolutePath

    $directChildResult = Invoke-Curl -Url $directChildUrl
    $serverChildResult = Invoke-Curl -Url $serverChildUrl

    if ($directChildResult.StatusCode -ne 200) {
        Write-Fail "Direct child unreachable: $directChildUrl (status $($directChildResult.StatusCode))"
        continue
    }
    if ($serverChildResult.StatusCode -ne 200) {
        Write-Fail "Server-resolved child unreachable: $serverChildUrl (status $($serverChildResult.StatusCode))"
        continue
    }

    $directChild = ConvertTo-NormalizedHlsStructure -ManifestText $directChildResult.Body -ManifestUrl $directChildUrl
    $serverChild = ConvertTo-NormalizedHlsStructure -ManifestText $serverChildResult.Body -ManifestUrl $serverChildUrl
    $directChildTags = $directChild.Tags -join '|'
    $serverChildTags = $serverChild.Tags -join '|'

    if ($directChildTags -eq $serverChildTags -and $directChild.ExtinfCount -eq $serverChild.ExtinfCount) {
        Write-Pass "Child '$label' structure identical ($($serverChild.ExtinfCount) segments)"
    } else {
        Write-Fail "Child '$label' structure differs`n  direct: $directChildTags ($($directChild.ExtinfCount) segments)`n  server: $serverChildTags ($($serverChild.ExtinfCount) segments)"
    }
}
if ($directMaster.Children.Count -ne $serverMaster.Children.Count) {
    Write-Fail "Child count mismatch: direct=$($directMaster.Children.Count) server=$($serverMaster.Children.Count)"
}

# --- expected-unreachable naive route ---

Write-Section 'Expected-unreachable naive route'
if ($serverMaster.Children.Count -gt 0) {
    $sampleFileName = Split-Path -Leaf ([Uri]$serverMaster.Children[0]).AbsolutePath
    $naiveUrl = "$ServerBase/media/$sampleFileName"
    $naiveResult = Invoke-Curl -Url $naiveUrl
    if ($naiveResult.StatusCode -eq 400) {
        Write-Pass "Naive server-relative child route still rejects as expected (400): $naiveUrl"
    } else {
        Write-Fail "Naive server-relative child route returned unexpected status $($naiveResult.StatusCode), expected 400: $naiveUrl"
    }
}

# --- summary ---

Write-Section 'Summary'
if ($script:ExitCode -eq 0) {
    Write-Host 'All checks passed.' -ForegroundColor Green
} else {
    Write-Host "$($script:Failures.Count) check(s) failed:" -ForegroundColor Red
    $script:Failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
}
exit $script:ExitCode
