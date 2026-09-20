param(
    [Parameter(Mandatory)] [ValidateNotNullOrEmpty()] [string] $ScannerSsid,
    [Parameter(Mandatory)] [ValidateNotNullOrEmpty()] [string] $ScannerPassword,
    [Parameter(Mandatory)] [ValidateNotNullOrEmpty()] [string] $InterfaceAlias,
    [System.Collections.IDictionary] $Operations,
    [scriptblock] $NativeNetshExecutor
)

$ErrorActionPreference = 'Stop'

function Invoke-NetshChecked {
    param([string[]] $Arguments)
    if ($null -eq $NativeNetshExecutor) {
        $result = & netsh @Arguments
        $exitCode = $LASTEXITCODE
    } else {
        $invocation = & $NativeNetshExecutor -commandArgs $Arguments
        $result = $invocation.Output
        $exitCode = $invocation.ExitCode
    }
    if ($exitCode -ne 0) { throw "netsh wlan operation failed (exit $exitCode): $($result -join ' ')" }
    return $result
}

function Get-WlanField {
    param([string] $Text, [string] $Field)
    $match = [regex]::Match($Text, '(?m)^\s*' + [regex]::Escape($Field) + '\s*:\s*(.*?)\s*$')
    if ($match.Success) { return $match.Groups[1].Value.Trim() }
    return ''
}

function New-DefaultOperations {
    return @{
        GetConnection = {
            param($alias)
            $output = Invoke-NetshChecked @('wlan', 'show', 'interfaces')
            $text = $output -join "`n"
            $blocks = [regex]::Matches($text, '(?ms)^\s*Name\s*:\s*(?<name>[^\r\n]+)\r?\n(?<body>.*?)(?=^\s*Name\s*:|\z)')
            foreach ($block in $blocks) {
                if ($block.Groups['name'].Value.Trim() -ne $alias) { continue }
                $body = $block.Groups['body'].Value
                return [pscustomobject]@{
                    State = Get-WlanField $body 'State'
                    Ssid = Get-WlanField $body 'SSID'
                    ProfileName = Get-WlanField $body 'Profile'
                }
            }
            throw "Wireless adapter '$alias' was not found."
        }
        ScanNetworks = {
            param($alias)
            $output = Invoke-NetshChecked @('wlan', 'show', 'networks', "interface=$alias", 'mode=bssid')
            return @($output | ForEach-Object {
                if ($_ -match '^\s*SSID\s+\d+\s*:\s*(.*?)\s*$') { $Matches[1] }
            })
        }
        AddProfile = {
            param($alias, $path)
            $null = Invoke-NetshChecked @('wlan', 'add', 'profile', "filename=$path", "interface=$alias", 'user=current')
        }
        ProfileExists = {
            param($alias, $profileName)
            $output = Invoke-NetshChecked @('wlan', 'show', 'profiles', "interface=$alias")
            foreach ($line in $output) {
                if ($line -match '^\s*(?:All User Profile|Current User Profile)\s*:\s*(.*?)\s*$' -and $Matches[1] -eq $profileName) {
                    return $true
                }
            }
            return $false
        }
        Connect = {
            param($alias, $profileName, $ssid)
            $null = Invoke-NetshChecked @('wlan', 'connect', "name=$profileName", "ssid=$ssid", "interface=$alias")
        }
        Disconnect = {
            param($alias)
            $null = Invoke-NetshChecked @('wlan', 'disconnect', "interface=$alias")
        }
        RemoveProfile = {
            param($alias, $profileName)
            $null = Invoke-NetshChecked @('wlan', 'delete', 'profile', "name=$profileName", "interface=$alias")
        }
        GetGateway = {
            param($alias)
            $configuration = Get-NetIPConfiguration -InterfaceAlias $alias
            return $configuration.IPv4DefaultGateway.NextHop
        }
        TestTcpPort = {
            param($gateway, $port)
            $client = [Net.Sockets.TcpClient]::new()
            try {
                $pending = $client.BeginConnect($gateway, $port, $null, $null)
                $open = $pending.AsyncWaitHandle.WaitOne(1500)
                if ($open) { try { $client.EndConnect($pending) } catch { $open = $false } }
                return $open
            } finally { $client.Close() }
        }
        GetUrl = {
            param($url)
            return Invoke-WebRequest -Uri $url -UseBasicParsing -TimeoutSec 4
        }
        Sleep = {
            param($seconds)
            Start-Sleep -Seconds $seconds
        }
    }
}

function Invoke-Operation {
    param([string] $Name, [object[]] $Arguments = @())
    return & $Operations[$Name] @Arguments
}

function Remove-CredentialXml {
    param([string] $Path)
    if (-not [IO.File]::Exists($Path)) { return }
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try {
        $remaining = $stream.Length
        $zeros = [byte[]]::new(4096)
        while ($remaining -gt 0) {
            $count = [int] [Math]::Min($remaining, $zeros.Length)
            $stream.Write($zeros, 0, $count)
            $remaining -= $count
        }
        $stream.Flush($true)
    } finally { $stream.Dispose() }
    [IO.File]::Delete($Path)
}

function Write-CredentialXml {
    param([string] $Path, [string] $ProfileName, [string] $Ssid, [string] $Password)
    $profileXml = [Security.SecurityElement]::Escape($ProfileName)
    $ssidXml = [Security.SecurityElement]::Escape($Ssid)
    $passwordXml = [Security.SecurityElement]::Escape($Password)
    $xml = @"
<?xml version="1.0"?>
<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">
  <name>$profileXml</name>
  <SSIDConfig><SSID><name>$ssidXml</name></SSID></SSIDConfig>
  <connectionType>ESS</connectionType>
  <connectionMode>manual</connectionMode>
  <MSM><security>
    <authEncryption><authentication>WPA2PSK</authentication><encryption>AES</encryption><useOneX>false</useOneX></authEncryption>
    <sharedKey><keyType>passPhrase</keyType><protected>false</protected><keyMaterial>$passwordXml</keyMaterial></sharedKey>
  </security></MSM>
</WLANProfile>
"@
    $stream = [IO.File]::Open($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try {
        $bytes = [Text.UTF8Encoding]::new($false).GetBytes($xml)
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush($true)
    } finally { $stream.Dispose() }
}

function Wait-ForConnection {
    param([string] $Alias, [string] $Ssid, [string] $ProfileName)
    for ($attempt = 0; $attempt -lt 30; $attempt++) {
        $current = Invoke-Operation 'GetConnection' @($Alias)
        if ($current.State -eq 'connected' -and $current.Ssid -eq $Ssid -and $current.ProfileName -eq $ProfileName) { return }
        if ($attempt -lt 29) { $null = Invoke-Operation 'Sleep' @(1) }
    }
    throw "Connection to '$Ssid' did not complete within 30 seconds."
}

function Wait-ForDisconnection {
    param([string] $Alias)
    for ($attempt = 0; $attempt -lt 30; $attempt++) {
        $current = Invoke-Operation 'GetConnection' @($Alias)
        if ($current.State -ne 'connected') { return }
        if ($attempt -lt 29) { $null = Invoke-Operation 'Sleep' @(1) }
    }
    throw "Adapter '$Alias' did not disconnect within 30 seconds."
}

$defaults = New-DefaultOperations
if ($null -eq $Operations) { $Operations = $defaults }
else {
    $merged = @{}
    foreach ($name in $defaults.Keys) { $merged[$name] = $defaults[$name] }
    foreach ($name in $Operations.Keys) { $merged[$name] = $Operations[$name] }
    $Operations = $merged
}
foreach ($required in @('GetConnection', 'ScanNetworks', 'AddProfile', 'ProfileExists', 'Connect', 'Disconnect', 'RemoveProfile', 'GetGateway', 'TestTcpPort', 'GetUrl', 'Sleep')) {
    if (-not $Operations.Contains($required) -or $Operations[$required] -isnot [scriptblock]) {
        throw "Operation '$required' must be supplied as a scriptblock."
    }
}

$original = Invoke-Operation 'GetConnection' @($InterfaceAlias)
if ($original.State -eq 'connected' -and (-not $original.Ssid -or -not $original.ProfileName)) {
    throw "Could not identify the original Wi-Fi connection on '$InterfaceAlias'."
}

$profileName = 'ES60W-Probe-' + [guid]::NewGuid().ToString('N')
$profilePath = Join-Path ([IO.Path]::GetTempPath()) ($profileName + '.xml')
$profileAddAttempted = $false
$primaryError = $null
$cleanupErrors = [System.Collections.Generic.List[Exception]]::new()

try {
    $visible = $false
    for ($scan = 0; $scan -lt 15; $scan++) {
        if (@(Invoke-Operation 'ScanNetworks' @($InterfaceAlias)) -contains $ScannerSsid) { $visible = $true; break }
        if ($scan -lt 14) { $null = Invoke-Operation 'Sleep' @(2) }
    }
    if (-not $visible) { throw "Scanner SSID '$ScannerSsid' is not visible on '$InterfaceAlias'." }

    if (Invoke-Operation 'ProfileExists' @($InterfaceAlias, $profileName)) {
        throw "Temporary profile name '$profileName' already exists on '$InterfaceAlias'."
    }
    Write-CredentialXml $profilePath $profileName $ScannerSsid $ScannerPassword
    $profileAddAttempted = $true
    $null = Invoke-Operation 'AddProfile' @($InterfaceAlias, $profilePath)
    Remove-CredentialXml $profilePath

    $null = Invoke-Operation 'Connect' @($InterfaceAlias, $profileName, $ScannerSsid)
    Wait-ForConnection $InterfaceAlias $ScannerSsid $profileName

    $gateway = Invoke-Operation 'GetGateway' @($InterfaceAlias)
    if (-not $gateway) { throw 'No scanner gateway address was assigned.' }
    Write-Output "Connected to scanner Wi-Fi. Scanner gateway: $gateway"
    foreach ($port in @(80, 443, 1865, 3289, 5357, 8080)) {
        $open = Invoke-Operation 'TestTcpPort' @($gateway, $port)
        Write-Output "TCP ${gateway}:$port = $open"
    }
    foreach ($url in @("http://$gateway/eSCL/ScannerCapabilities", "http://$gateway/eSCL/ScannerStatus")) {
        try {
            $response = Invoke-Operation 'GetUrl' @($url)
            Write-Output "GET $url = $($response.StatusCode), $($response.Content.Length) bytes"
            $sample = [string] $response.Content
            Write-Output $sample.Substring(0, [Math]::Min(300, $sample.Length))
        } catch { Write-Output "GET $url failed: $($_.Exception.Message)" }
    }
} catch {
    $primaryError = $_.Exception
} finally {
    try { Remove-CredentialXml $profilePath } catch { $cleanupErrors.Add($_.Exception) }
    if ($profileAddAttempted) {
        try {
            if ($original.State -eq 'connected') {
                $null = Invoke-Operation 'Connect' @($InterfaceAlias, $original.ProfileName, $original.Ssid)
                Wait-ForConnection $InterfaceAlias $original.Ssid $original.ProfileName
            } else {
                $null = Invoke-Operation 'Disconnect' @($InterfaceAlias)
                Wait-ForDisconnection $InterfaceAlias
            }
        } catch { $cleanupErrors.Add([Exception]::new("Could not restore original connection: $($_.Exception.Message)", $_.Exception)) }
        try {
            if (Invoke-Operation 'ProfileExists' @($InterfaceAlias, $profileName)) {
                $null = Invoke-Operation 'RemoveProfile' @($InterfaceAlias, $profileName)
            }
        }
        catch { $cleanupErrors.Add([Exception]::new("Could not remove temporary profile: $($_.Exception.Message)", $_.Exception)) }
    }
}

if ($primaryError -and $cleanupErrors.Count -gt 0) {
    $all = [System.Collections.Generic.List[Exception]]::new()
    $all.Add($primaryError)
    $all.AddRange($cleanupErrors)
    throw [AggregateException]::new('Scanner probe and restoration both failed.', $all.ToArray())
}
if ($primaryError) { throw $primaryError }
if ($cleanupErrors.Count -gt 0) { throw [AggregateException]::new('Scanner probe restoration failed.', $cleanupErrors.ToArray()) }
Write-Output "Returned '$InterfaceAlias' to its original connection and removed the temporary scanner profile."
