param([string]$ip, [string]$st)
$client = New-Object System.Net.Sockets.UdpClient
$client.Client.ReceiveTimeout = 2000
$endpoint = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Parse($ip), 1900)
$message = "M-SEARCH * HTTP/1.1`r`nHOST: 239.255.255.250:1900`r`nMAN: `"ssdp:discover`"`r`nMX: 1`r`nST: $st`r`n`r`n"
$bytes = [System.Text.Encoding]::ASCII.GetBytes($message)
[void]$client.Send($bytes, $bytes.Length, $endpoint)
$remote = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, 0)
try {
    $resp = $client.Receive([ref]$remote)
    Write-Host [System.Text.Encoding]::ASCII.GetString($resp)
} catch {
    Write-Host "No response from $ip"
}
$client.Close()
