param(
    [string]$RemoteHost,
    [int]$Port = 8022,
    [string]$EnvPath = ".env",
    [switch]$InstallTools,
    [switch]$KeepFirewallRules
)

$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$envFile = Join-Path $repo $EnvPath
$plink = "C:\Program Files\PuTTY\plink.exe"
$pscp = "C:\Program Files\PuTTY\pscp.exe"
$remoteRoot = "/data/data/com.termux/files/home/dlna-server-posix"
$archive = Join-Path $env:TEMP "dlna-server-posix-src.tar.gz"
$hostKey = "ssh-ed25519 255 SHA256:f8SnVrlYawESpiziyj09fPFoLfWXKIBBhoRVG21RBrc"
$firewallRules = @("DLNA Test UDP 1900")

function Read-DotEnv {
    $values = @{}
    Get-Content -LiteralPath $envFile | ForEach-Object {
        $line = $_.Trim()
        if (-not $line -or $line.StartsWith("#") -or $line -notmatch "=") { return }
        $idx = $line.IndexOf("=")
        $key = $line.Substring(0, $idx).Trim()
        if (-not $values.ContainsKey($key)) {
            $values[$key] = $line.Substring($idx + 1).Trim()
        }
    }
    return $values
}

function Run-Remote {
    param([string]$Script, [int]$TimeoutSec = 120)
    $cmdFile = Join-Path $env:TEMP ("termux-" + [guid]::NewGuid().ToString() + ".sh")
    Set-Content -LiteralPath $cmdFile -Value $Script -Encoding ascii
    try {
        & $plink -ssh -batch -P $Port -pw $password -no-antispoof -hostkey $hostKey -m $cmdFile "$username@$RemoteHost"
        if ($LASTEXITCODE -ne 0) { throw "Remote command failed with exit $LASTEXITCODE" }
    } finally {
        Remove-Item -LiteralPath $cmdFile -Force -ErrorAction SilentlyContinue
    }
}

function Invoke-MSearch {
    param([string]$TargetIp, [bool]$Multicast)
    $client = [System.Net.Sockets.UdpClient]::new([System.Net.Sockets.AddressFamily]::InterNetwork)
    try {
        $client.Client.ReceiveTimeout = 5000
        $dest = if ($Multicast) { "239.255.255.250" } else { $TargetIp }
        $msg = @(
            "M-SEARCH * HTTP/1.1",
            "HOST: 239.255.255.250:1900",
            'MAN: "ssdp:discover"',
            "MX: 1",
            "ST: urn:schemas-upnp-org:device:MediaServer:1",
            "",
            ""
        ) -join "`r`n"
        $bytes = [Text.Encoding]::ASCII.GetBytes($msg)
        [void]$client.Send($bytes, $bytes.Length, [Net.IPEndPoint]::new([Net.IPAddress]::Parse($dest), 1900))
        $remote = [Net.IPEndPoint]::new([Net.IPAddress]::Any, 0)
        $buf = $client.Receive([ref]$remote)
        $text = [Text.Encoding]::ASCII.GetString($buf)
        return "PASS SSDP " + ($(if ($Multicast) { "multicast" } else { "unicast" })) + " from " + $remote.ToString() + "`n" +
            (($text -split "`r?`n" | Where-Object { $_ -match "^(HTTP/|LOCATION:|ST:|USN:|SERVER:)" }) -join "`n")
    } catch {
        return "WARN SSDP " + ($(if ($Multicast) { "multicast" } else { "unicast" })) + " failed: " + $_.Exception.Message
    } finally {
        $client.Close()
    }
}

function Start-SsdpNotifyListener {
    param([string]$ExpectedHost)

    return Start-Job -ScriptBlock {
        param([string]$ExpectedHost)

        $client = [System.Net.Sockets.UdpClient]::new([System.Net.Sockets.AddressFamily]::InterNetwork)
        $client.Client.SetSocketOption([System.Net.Sockets.SocketOptionLevel]::Socket, [System.Net.Sockets.SocketOptionName]::ReuseAddress, $true)
        $client.Client.Bind([System.Net.IPEndPoint]::new([System.Net.IPAddress]::Any, 1900))

        $localIps = [System.Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces() |
            Where-Object { $_.OperationalStatus -eq [System.Net.NetworkInformation.OperationalStatus]::Up } |
            ForEach-Object { $_.GetIPProperties().UnicastAddresses } |
            Where-Object { $_.Address.AddressFamily -eq [System.Net.Sockets.AddressFamily]::InterNetwork -and $_.Address.ToString() -ne "127.0.0.1" } |
            ForEach-Object { $_.Address }

        foreach ($ip in $localIps) {
            try {
                $client.JoinMulticastGroup([System.Net.IPAddress]::Parse("239.255.255.250"), $ip)
            } catch {
            }
        }

        $deadline = (Get-Date).AddSeconds(15)
        while ((Get-Date) -lt $deadline) {
            try {
                $client.Client.ReceiveTimeout = 1000
                $remote = [System.Net.IPEndPoint]::new([System.Net.IPAddress]::Any, 0)
                $buf = $client.Receive([ref]$remote)
                $text = [System.Text.Encoding]::ASCII.GetString($buf)
                if ($remote.Address.ToString() -eq $ExpectedHost -and
                    $text -match "NOTIFY \* HTTP/1.1" -and
                    $text -match "NTS:\s*ssdp:alive" -and
                    $text -match "NT:\s*urn:schemas-upnp-org:device:MediaServer:1" -and
                    $text -match "LOCATION:\s*http://$([regex]::Escape($ExpectedHost)):18200/description.xml") {
                    $client.Close()
                    return "PASS SSDP NOTIFY alive from $($remote.ToString())`n" +
                        (($text -split "`r?`n" | Where-Object { $_ -match "^(NOTIFY|LOCATION:|NT:|NTS:|USN:|SERVER:)" }) -join "`n")
                }
            } catch {
            }
        }

        $client.Close()
        throw "No matching SSDP alive NOTIFY received from $ExpectedHost"
    } -ArgumentList $ExpectedHost
}

function Add-TemporaryFirewallRules {
    Get-NetFirewallRule -DisplayName "DLNA Test UDP 1900" -ErrorAction SilentlyContinue | Remove-NetFirewallRule
    New-NetFirewallRule -DisplayName "DLNA Test UDP 1900" -Direction Inbound -Action Allow -Protocol UDP -LocalPort 1900 -Profile Any | Out-Null
}

function Remove-TemporaryFirewallRules {
    if ($KeepFirewallRules) { return }
    foreach ($rule in $firewallRules) {
        Get-NetFirewallRule -DisplayName $rule -ErrorAction SilentlyContinue | Remove-NetFirewallRule
    }
}

if (-not (Test-Path $envFile)) { throw "Missing $envFile" }
if (-not (Test-Path $plink) -or -not (Test-Path $pscp)) { throw "Install PuTTY first: winget install --id PuTTY.PuTTY -e" }

$envValues = Read-DotEnv
$username = $envValues["username"]
$password = $envValues["password"]
if (-not $username -or -not $password) { throw ".env must contain username and password" }
if ($envValues["port"] -and -not $PSBoundParameters.ContainsKey("Port")) {
    $Port = [int]$envValues["port"]
}
if (-not $RemoteHost) {
    $ipOutput = & adb shell "ip -4 addr show wlan0" 2>$null
    $match = [regex]::Match(($ipOutput | Out-String), "inet\s+(\d+\.\d+\.\d+\.\d+)/")
    if (-not $match.Success) { throw "Host omitted and adb wlan0 IP not found" }
    $RemoteHost = $match.Groups[1].Value
}

if ($InstallTools) {
    Run-Remote "pkg update -y; pkg install -y clang cmake make python; command -v cmake; command -v clang++; command -v python; exit" 300
}

try {
    Add-TemporaryFirewallRules

    if (Test-Path $archive) { Remove-Item -LiteralPath $archive -Force }
    tar -czf $archive CMakeLists.txt src macos_port_workflow_dlna_server_repo.md
    & $pscp -batch -P $Port -pw $password -hostkey $hostKey $archive "$username@$RemoteHost`:/data/data/com.termux/files/home/dlna-server-posix-src.tar.gz"
    if ($LASTEXITCODE -ne 0) { throw "pscp failed" }

    Run-Remote @"
set -e
cd ~
kill `$(cat $remoteRoot/posix-cross.pid) 2>/dev/null || true
rm -rf $remoteRoot
mkdir -p $remoteRoot
cd $remoteRoot
tar -xzf ~/dlna-server-posix-src.tar.gz
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -- -j1
rm -rf testmedia posix-cross.log posix-cross.pid
mkdir -p testmedia
printf 'fake mp3 bytes' > testmedia/song.mp3
exit
"@ 300

    $notifyJob = Start-SsdpNotifyListener -ExpectedHost $RemoteHost

    Run-Remote @"
set -e
cd $remoteRoot
nohup ./build/dlna-server --port 18200 --name 'Termux POSIX DLNA' --uuid 22222222-3333-4444-5555-666666666666 --debug --source "`$PWD/testmedia" >posix-cross.log 2>&1 &
echo `$! > posix-cross.pid
sleep 3
cat posix-cross.log
exit
"@ 300

    Wait-Job $notifyJob | Out-Null
    Write-Host (Receive-Job $notifyJob)
    Remove-Job $notifyJob

    $desc = Invoke-WebRequest -Uri "http://$RemoteHost`:18200/description.xml" -UseBasicParsing -TimeoutSec 8
    if ($desc.Content -match "Termux POSIX DLNA" -and $desc.Content -match "ContentDirectory:1") {
        Write-Host "PASS Windows fetched POSIX description.xml"
    } else {
        throw "description.xml missing expected DLNA XML"
    }

    Write-Host (Invoke-MSearch -TargetIp $RemoteHost -Multicast:$false)
} finally {
    try {
        Run-Remote "cd $remoteRoot; kill `$(cat posix-cross.pid) 2>/dev/null || true; exit" 30
    } catch {
    }
    Remove-TemporaryFirewallRules
}
