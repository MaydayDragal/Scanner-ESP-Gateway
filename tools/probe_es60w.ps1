param(
    [Parameter(Mandatory)] [string] $ScannerSsid,
    [Parameter(Mandatory)] [string] $ScannerPassword
)

$ErrorActionPreference = 'Stop'
$interfaceName = 'Wi-Fi'
$profilePath = Join-Path $env:TEMP ("es60w-" + [guid]::NewGuid().ToString('N') + '.xml')
$originalSsid = ((& netsh wlan show interfaces) | Select-String '^\s*SSID\s*:\s*(.+)$' | Select-Object -First 1).Matches.Groups[1].Value.Trim()
if (-not $originalSsid) { throw 'Wi-Fi is not connected; cannot restore its original network.' }

$visible = (& netsh wlan show networks mode=bssid) | Select-String ('^SSID\s+\d+\s*:\s*' + [regex]::Escape($ScannerSsid) + '\s*$')
if (-not $visible) { throw "Scanner SSID '$ScannerSsid' is not visible to this computer." }

try {
    $ssidXml = [System.Security.SecurityElement]::Escape($ScannerSsid)
    $passwordXml = [System.Security.SecurityElement]::Escape($ScannerPassword)
    $xml = @"
<?xml version="1.0"?>
<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">
  <name>$ssidXml</name>
  <SSIDConfig><SSID><name>$ssidXml</name></SSID></SSIDConfig>
  <connectionType>ESS</connectionType>
  <connectionMode>manual</connectionMode>
  <MSM><security>
    <authEncryption><authentication>WPA2PSK</authentication><encryption>AES</encryption><useOneX>false</useOneX></authEncryption>
    <sharedKey><keyType>passPhrase</keyType><protected>false</protected><keyMaterial>$passwordXml</keyMaterial></sharedKey>
  </security></MSM>
</WLANProfile>
"@
    [IO.File]::WriteAllText($profilePath, $xml, [Text.Encoding]::UTF8)
    $null = & netsh wlan add profile "filename=$profilePath" user=current
    if ($LASTEXITCODE -ne 0) { throw 'Could not add scanner Wi-Fi profile.' }
    Remove-Item -LiteralPath $profilePath -Force

    $null = & netsh wlan connect "name=$ScannerSsid" "ssid=$ScannerSsid" "interface=$interfaceName"
    if ($LASTEXITCODE -ne 0) { throw 'Could not start scanner Wi-Fi connection.' }
    $connected = $false
    for ($i = 0; $i -lt 30; $i++) {
        Start-Sleep -Seconds 1
        $current = ((& netsh wlan show interfaces) | Select-String '^\s*SSID\s*:\s*(.+)$' | Select-Object -First 1).Matches.Groups[1].Value.Trim()
        if ($current -eq $ScannerSsid) { $connected = $true; break }
    }
    if (-not $connected) { throw 'Scanner Wi-Fi did not connect within 30 seconds.' }

    $network = Get-NetIPConfiguration -InterfaceAlias $interfaceName
    $gateway = $network.IPv4DefaultGateway.NextHop
    Write-Output "Connected to scanner Wi-Fi. Scanner gateway: $gateway"
    if (-not $gateway) { throw 'No scanner gateway address was assigned.' }

    foreach ($port in @(80, 443, 1865, 3289, 5357, 8080)) {
        $client = [Net.Sockets.TcpClient]::new()
        try {
            $async = $client.BeginConnect($gateway, $port, $null, $null)
            $open = $async.AsyncWaitHandle.WaitOne(1500)
            if ($open) { try { $client.EndConnect($async) } catch { $open = $false } }
            Write-Output "TCP ${gateway}:$port = $open"
        } finally { $client.Close() }
    }

    foreach ($url in @("http://$gateway/eSCL/ScannerCapabilities", "http://$gateway/eSCL/ScannerStatus")) {
        try {
            $response = Invoke-WebRequest -Uri $url -UseBasicParsing -TimeoutSec 4
            Write-Output "GET $url = $($response.StatusCode), $($response.Content.Length) bytes"
            $sample = [string]$response.Content
            Write-Output $sample.Substring(0, [Math]::Min(300, $sample.Length))
        } catch { Write-Output "GET $url failed: $($_.Exception.Message)" }
    }
} finally {
    Remove-Item -LiteralPath $profilePath -Force -ErrorAction SilentlyContinue
    $null = & netsh wlan connect "name=$originalSsid" "interface=$interfaceName"
    for ($i = 0; $i -lt 30; $i++) {
        Start-Sleep -Seconds 1
        $current = ((& netsh wlan show interfaces) | Select-String '^\s*SSID\s*:\s*(.+)$' | Select-Object -First 1).Matches.Groups[1].Value.Trim()
        if ($current -eq $originalSsid) { break }
    }
    $null = & netsh wlan delete profile "name=$ScannerSsid" "interface=$interfaceName"
    Write-Output "Returned to $originalSsid and removed the temporary scanner profile."
}
