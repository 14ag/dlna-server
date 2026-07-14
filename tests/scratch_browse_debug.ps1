$PC  = "http://localhost:8200/upnp/control/content_directory"
function Browse-DLNA($url, $id, $flag) {
    $b = "<?xml version=`"1.0`"?><s:Envelope xmlns:s=`"http://schemas.xmlsoap.org/soap/envelope/`"><s:Body><u:Browse xmlns:u=`"urn:schemas-upnp-org:service:ContentDirectory:1`"><ObjectID>$id</ObjectID><BrowseFlag>$flag</BrowseFlag><Filter>*</Filter><StartingIndex>0</StartingIndex><RequestedCount>100</RequestedCount><SortCriteria></SortCriteria></u:Browse></s:Body></s:Envelope>"
    $bf = "$env:TEMP\dlna_browse_$(($id -replace '[^a-z0-9]','_')).xml"
    [System.IO.File]::WriteAllText($bf, $b, [System.Text.Encoding]::UTF8)
    $raw = C:\Windows\System32\curl.exe --connect-timeout 5 -s $url -X POST -H "Content-Type: text/xml" -H "SOAPACTION: urn:schemas-upnp-org:service:ContentDirectory:1#Browse" --data-binary "@$bf"
    return $raw
}

Write-Host "PC Raw Output:"
$pcRaw = Browse-DLNA $PC  "1000000" "BrowseDirectChildren"
Write-Host $pcRaw
