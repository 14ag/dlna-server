$PC = "http://localhost:8200/upnp/control/content_directory"
$configPath = "output\winx64\config.ini"
$exePath = "output\winx64\DLNA Server.exe"
$mediaDir = "tests\test media\test-hls-playlist.m3u8"

# --------------- config ---------------
function Set-IniValue($section, $key, $value) {
    if (Test-Path $configPath) { $lines = @(Get-Content -LiteralPath $configPath -Encoding UTF8) }
    else { $lines = @("[$section]") }
    $sectionHeader = "[$section]"
    $sectionIndex = [Array]::IndexOf([string[]]$lines, $sectionHeader)
    if ($sectionIndex -lt 0) { $lines += $sectionHeader; $sectionIndex = $lines.Count - 1 }
    $keyPrefix = "$key="
    $insertIndex = $lines.Count
    for ($i = $sectionIndex + 1; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^\[.+\]$') { $insertIndex = $i; break }
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

# --------------- server lifecycle ---------------
function Stop-RepoDlnaProcesses {
    Get-Process "DLNA Server" -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Seconds 1
}

function Start-DlnaServer($proxySetting) {
    Stop-RepoDlnaProcesses
    Set-IniValue "Settings" "DebugLog" "1"
    Set-IniValue "Settings" "ProxyStreams" $proxySetting
    Set-IniValue "Settings" "MediaSources" $mediaDir
    Set-IniValue "Settings" "Port" "8200"
    $p = Start-Process -FilePath $exePath -ArgumentList "-h" -NoNewWindow -PassThru
    Start-Sleep -Seconds 5
    $tcp = Get-NetTCPConnection -State Listen -LocalPort 8200 -ErrorAction SilentlyContinue
    if (-not $tcp) { Write-Host "ERROR: Server not listening on 8200"; exit 1 }
    return $p
}

function Stop-DlnaServer($proc) {
    if ($proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
    Stop-RepoDlnaProcesses
}

# --------------- SOAP browse ---------------
function Browse-DLNA($url, $id, $flag) {
    $b = "<?xml version=`"1.0`"?><s:Envelope xmlns:s=`"http://schemas.xmlsoap.org/soap/envelope/`"><s:Body><u:Browse xmlns:u=`"urn:schemas-upnp-org:service:ContentDirectory:1`"><ObjectID>$id</ObjectID><BrowseFlag>$flag</BrowseFlag><Filter>*</Filter><StartingIndex>0</StartingIndex><RequestedCount>100</RequestedCount><SortCriteria></SortCriteria></u:Browse></s:Body></s:Envelope>"
    $bf = "$env:TEMP\dlna_browse_$(($id -replace '[^a-z0-9]','_')).xml"
    [System.IO.File]::WriteAllText($bf, $b, [System.Text.Encoding]::UTF8)
    $raw = C:\Windows\System32\curl.exe --connect-timeout 5 -s $url -X POST -H "Content-Type: text/xml" -H "SOAPACTION: `"urn:schemas-upnp-org:service:ContentDirectory:1#Browse`"" --data-binary "@$bf"
    Remove-Item -LiteralPath $bf -Force -ErrorAction SilentlyContinue
    return ($raw -join "`n")
}

function Get-DIDL($soap) {
    $s = $soap.IndexOf("<Result>")
    $e = $soap.IndexOf("</Result>")
    if ($s -ge 0 -and $e -gt $s) { return $soap.Substring($s + 8, $e - $s - 8) }
    return $null
}

function Get-DidlXml($raw) {
    $didl = Get-DIDL $raw
    if (-not $didl) { return $null }
    Add-Type -AssemblyName System.Web -ErrorAction SilentlyContinue
    [xml][System.Web.HttpUtility]::HtmlDecode($didl)
}

# --------------- comparison ---------------
function Field($node, $name) {
    $el = $node.SelectSingleNode("*[local-name()='$name']")
    if ($el) { return $el.InnerText } else { return "" }
}

function Attr($node, $name) {
    $a = $node.Attributes[$name]
    if ($a) { return $a.Value } else { return "" }
}



function ResProto($node) {
    $res = $node.SelectSingleNode("*[local-name()='res']")
    if ($res) { return $res.GetAttribute('protocolInfo') } else { return "" }
}

function ResURL($node) {
    $res = $node.SelectSingleNode("*[local-name()='res']")
    if ($res) { return $res.InnerText } else { return "" }
}

function Get-ChildOrder($node) {
    $out = @()
    foreach ($c in $node.ChildNodes) {
        if ($c.NodeType -eq 'Element') { $out += $c.LocalName }
    }
    return $out
}

# --------------- MAIN ---------------
function Test-Server($proxy) {
    $serverProc = $null
    try {
        $serverProc = Start-DlnaServer -proxySetting $proxy

        # Discover container hierarchy dynamically
        $rootRaw = Browse-DLNA $PC "0" "BrowseDirectChildren"
        $rootXml = Get-DidlXml $rootRaw
        if (-not $rootXml) { Write-Host "ERROR: Could not parse root"; exit 1 }

        $fileContainer = $rootXml.SelectSingleNode("//*[local-name()='container']")
        if (-not $fileContainer) { Write-Host "ERROR: No container found in root"; exit 1 }
        $fileContainerId = $fileContainer.GetAttribute("id")
        Write-Host "File container: id=$fileContainerId title=$(Field $fileContainer 'title')"

        # Browse file container to find playlist folder or item
        $childRaw = Browse-DLNA $PC $fileContainerId "BrowseDirectChildren"
        $childXml = Get-DidlXml $childRaw
        if (-not $childXml) { Write-Host "ERROR: Could not parse file container"; exit 1 }

        $subContainer = $childXml.SelectSingleNode("//*[local-name()='container']")
        $itemNode = $childXml.SelectSingleNode("//*[local-name()='item']")

        if ($subContainer) {
            Write-Host "Found sub-container, browsing deeper..."
            $subId = $subContainer.GetAttribute("id")
            $itemRaw = Browse-DLNA $PC $subId "BrowseDirectChildren"
            $itemXml = Get-DidlXml $itemRaw
            if (-not $itemXml) { Write-Host "ERROR: Could not parse sub-container"; exit 1 }
            $pcItem = $itemXml.SelectSingleNode("//*[local-name()='item']")
        }
        else {
            $pcItem = $itemNode
        }

        if (-not $pcItem) { Write-Host "ERROR: No item found in PC tree"; exit 1 }
        Write-Host "PC item: restricted=$(Attr $pcItem 'restricted') title=$(Field $pcItem 'title') class=$(Field $pcItem 'class')"

        # Hardcoded Android expected values
        $android = @{
            restricted   = "1"
            title        = "ABC 13 Asheville NC (WLOS) (1080p)"
            class        = "object.item.videoItem"
            date         = Get-Date -Format "yyyy-MM-dd"
            protocolInfo = "http-get:*:video/mpegurl:DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01700000000000000000000000000000"
            url          = "https://aegis-cloudfront-1.tubi.video/ec903a48-3638-4d0b-ac89-813e147bca58/playlist.m3u8"
            elementOrder = @("title", "class", "date", "res")
        }

        Write-Host "`n============================== CONTENT FIELDS =============================="
        $fields = @(
            @{ Name = "[ATTR] restricted"; PC = Attr $pcItem 'restricted'; AND = $android.restricted },
            @{ Name = "dc:title"; PC = Field $pcItem 'title'; AND = $android.title },
            @{ Name = "upnp:class"; PC = Field $pcItem 'class'; AND = $android.class },
            @{ Name = "dc:date"; PC = Field $pcItem 'date'; AND = $android.date },
            @{ Name = "res protocolInfo"; PC = ResProto $pcItem; AND = $android.protocolInfo },
            @{ Name = "res (URL)"; PC = ResURL $pcItem; AND = $android.url }
        )
        $allMatch = $true
        foreach ($f in $fields) {
            $match = if ($f.PC -eq $f.AND) { "MATCH" } else { "DIFF"; $allMatch = $false }
            Write-Host "[$match] $($f.Name)"
            if ($match -eq "DIFF") { Write-Host "  PC : $($f.PC)`n  AND: $($f.AND)" }
            else { Write-Host "  $($f.PC)" }
        }

        Write-Host "`n============================== ELEMENT ORDER =============================="
        $pcOrder = Get-ChildOrder $pcItem
        $orderMatch = ($pcOrder -join ',') -eq ($android.elementOrder -join ',')
        if (-not $orderMatch) { $allMatch = $false }
        Write-Host "$(if ($orderMatch) { '[MATCH]' } else { '[DIFF]' }) Child element order"
        Write-Host "  PC : $($pcOrder -join ' -> ')"
        Write-Host "  AND: $($android.elementOrder -join ' -> ')"

        if ($allMatch) { Write-Host "`nPASS: All fields match Android expected values" -ForegroundColor Green }
        else { Write-Host "`nFAIL: Some fields differ from Android expected values" -ForegroundColor Red; exit 1 }
    }
    finally {
        Stop-DlnaServer $serverProc
    }
}

Test-Server -proxy "0"
Write-Host ""
Write-Host ""
Write-Host "now with proxy on ----------------------------------------------------------------"
Test-Server -proxy "1"
