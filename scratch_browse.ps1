$PC  = "http://localhost:8200/upnp/control/content_directory"
$AND = "http://192.168.100.33:38520/service/ContentDirectory_control"

function Browse-DLNA($url, $id, $flag) {
    $b = "<?xml version=`"1.0`"?><s:Envelope xmlns:s=`"http://schemas.xmlsoap.org/soap/envelope/`"><s:Body><u:Browse xmlns:u=`"urn:schemas-upnp-org:service:ContentDirectory:1`"><ObjectID>$id</ObjectID><BrowseFlag>$flag</BrowseFlag><Filter>*</Filter><StartingIndex>0</StartingIndex><RequestedCount>100</RequestedCount><SortCriteria></SortCriteria></u:Browse></s:Body></s:Envelope>"
    $bf = "$env:TEMP\dlna_browse_$(($id -replace '[^a-z0-9]','_')).xml"
    [System.IO.File]::WriteAllText($bf, $b, [System.Text.Encoding]::UTF8)
    $raw = C:\Windows\System32\curl.exe --connect-timeout 5 -s $url -X POST -H "Content-Type: text/xml" -H "SOAPACTION: `"urn:schemas-upnp-org:service:ContentDirectory:1#Browse`"" --data-binary "@$bf"
    return ($raw -join "`n")
}

$pcRaw  = Browse-DLNA $PC  "1000000" "BrowseDirectChildren"
$andRaw = Browse-DLNA $AND 'p$0'     "BrowseDirectChildren"

if (-not $pcRaw) { Write-Host "ERROR: PC raw is empty"; exit }
if (-not $andRaw) { Write-Host "ERROR: AND raw is empty"; exit }

# strip SOAP envelope - grab raw DIDL
function Get-DIDL($soap) {
    $s = $soap.IndexOf("<Result>")
    $e = $soap.IndexOf("</Result>")
    if ($s -ge 0 -and $e -gt $s) {
        return $soap.Substring($s + 8, $e - $s - 8)
    }
    return $null
}

$pcDidl  = Get-DIDL $pcRaw
$andDidl = Get-DIDL $andRaw

if (-not $pcDidl) { Write-Host "ERROR: PC Result not found in:`n$pcRaw"; exit }
if (-not $andDidl) { Write-Host "ERROR: AND Result not found in:`n$andRaw"; exit }

Add-Type -AssemblyName System.Web
$pcXml  = [System.Web.HttpUtility]::HtmlDecode($pcDidl)
$andXml = [System.Web.HttpUtility]::HtmlDecode($andDidl)

[xml]$pcDoc  = $pcXml
[xml]$andDoc = $andXml

$pcItem  = $pcDoc.SelectSingleNode("//*[local-name()='item']")
$andItem = $andDoc.SelectSingleNode("//*[local-name()='item']")

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

$fields = @(
    @{ Name="[ATTR] restricted";   PC=Attr $pcItem 'restricted';  AND=Attr $andItem 'restricted'  },
    @{ Name="dc:title";            PC=Field $pcItem 'title';      AND=Field $andItem 'title'      },
    @{ Name="upnp:class";          PC=Field $pcItem 'class';      AND=Field $andItem 'class'      },
    @{ Name="dc:date";             PC=Field $pcItem 'date';       AND=Field $andItem 'date'       },
    @{ Name="res protocolInfo";    PC=ResProto $pcItem;           AND=ResProto $andItem          },
    @{ Name="res (URL)";           PC=ResURL $pcItem;             AND=ResURL $andItem            }
)

Write-Host "============================== CONTENT FIELDS =============================="
foreach ($f in $fields) {
    $match = if ($f.PC -eq $f.AND) { "MATCH" } else { "DIFF" }
    Write-Host "[$match] $($f.Name)"
    if ($match -eq "DIFF") {
        Write-Host "  PC : $($f.PC)"
        Write-Host "  AND: $($f.AND)"
    } else {
        Write-Host "  $($f.PC)"
    }
}

Write-Host "`n============================== ELEMENT ORDER =============================="
function Get-ChildOrder($node) {
    $out = @()
    foreach ($c in $node.ChildNodes) {
        if ($c.NodeType -eq 'Element') { $out += $c.LocalName }
    }
    return $out
}
$pcOrder  = Get-ChildOrder $pcItem
$andOrder = Get-ChildOrder $andItem
$orderMatch = ($pcOrder -join ',') -eq ($andOrder -join ',')
$orderStatus = if ($orderMatch) { "MATCH" } else { "DIFF" }
Write-Host "[$orderStatus] Child element order"
Write-Host "  PC : $($pcOrder -join ' -> ')"
Write-Host "  AND: $($andOrder -join ' -> ')"
