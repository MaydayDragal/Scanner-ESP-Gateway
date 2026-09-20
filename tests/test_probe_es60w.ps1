$ErrorActionPreference = 'Stop'
$probe = Join-Path $PSScriptRoot '..\tools\probe_es60w.ps1'
$passed = 0

# An accidental bypass of the injected operations must fail before Windows networking is touched.
function global:netsh { throw 'Real netsh is forbidden during probe tests.' }

function Assert-True([bool] $Condition, [string] $Message) {
    if (-not $Condition) { throw $Message }
}

function New-FakeEnvironment {
    param(
        [switch] $ExistingScannerProfile,
        [switch] $FailAfterAdd,
        [switch] $FailScannerConnect,
        [switch] $FailRestore,
        [string] $OriginalSsid = 'Home Network'
    )

    $profiles = @{'Home Profile' = 'Home Network'}
    if ($ExistingScannerProfile) { $profiles['Epson Existing'] = 'DIRECT-ES60W' }
    $state = [pscustomobject]@{
        Profiles = $profiles
        Connections = @{'Wi-Fi 1' = [pscustomobject]@{State='connected'; Ssid=$OriginalSsid; ProfileName='Home Profile'};
                        'Wi-Fi 2' = [pscustomobject]@{State='connected'; Ssid='Office'; ProfileName='Office Profile'}}
        Calls = [System.Collections.ArrayList]::new()
        XmlPaths = [System.Collections.ArrayList]::new()
        FailScannerConnect = $FailScannerConnect
        FailAfterAdd = $FailAfterAdd
        FailRestore = $FailRestore
    }

    $ops = @{
        GetConnection = { param($alias)
            [void] $state.Calls.Add("get:$alias")
            if (-not $state.Connections.ContainsKey($alias)) { throw "Unknown adapter: $alias" }
            return $state.Connections[$alias]
        }.GetNewClosure()
        ScanNetworks = { param($alias)
            [void] $state.Calls.Add("scan:$alias")
            return @('DIRECT-ES60W')
        }.GetNewClosure()
        AddProfile = { param($alias, $path)
            [void] $state.Calls.Add("add:$alias")
            [void] $state.XmlPaths.Add($path)
            [xml] $xml = Get-Content -LiteralPath $path -Raw
            $profileName = $xml.WLANProfile.name
            $ssid = $xml.WLANProfile.SSIDConfig.SSID.name
            if ($state.Profiles.ContainsKey($profileName)) { throw 'Temporary profile name collided' }
            $state.Profiles[$profileName] = $ssid
            if ($state.FailAfterAdd) { throw 'add reported failure after creating profile' }
        }.GetNewClosure()
        ProfileExists = { param($alias, $profileName)
            [void] $state.Calls.Add("exists:$alias`:$profileName")
            return $state.Profiles.ContainsKey($profileName)
        }.GetNewClosure()
        Connect = { param($alias, $profileName, $ssid)
            [void] $state.Calls.Add("connect:$alias`:$profileName")
            if ($alias -ne 'Wi-Fi 1') { throw 'Wrong adapter touched' }
            if ($profileName -eq 'Home Profile' -and $state.FailRestore) { throw 'restore connection failed' }
            if ($profileName -ne 'Home Profile' -and $state.FailScannerConnect) { throw 'scanner connection failed' }
            if (-not $state.Profiles.ContainsKey($profileName)) { throw "Missing profile: $profileName" }
            $state.Connections[$alias] = [pscustomobject]@{State='connected'; Ssid=$state.Profiles[$profileName]; ProfileName=$profileName}
        }.GetNewClosure()
        Disconnect = { param($alias)
            [void] $state.Calls.Add("disconnect:$alias")
            $state.Connections[$alias] = [pscustomobject]@{State='disconnected'; Ssid=''; ProfileName=''}
        }.GetNewClosure()
        RemoveProfile = { param($alias, $profileName)
            [void] $state.Calls.Add("remove:$alias`:$profileName")
            if (-not $state.Profiles.ContainsKey($profileName)) { throw "Missing profile: $profileName" }
            $state.Profiles.Remove($profileName)
        }.GetNewClosure()
        GetGateway = { param($alias)
            [void] $state.Calls.Add("gateway:$alias")
            return '192.0.2.1'
        }.GetNewClosure()
        TestTcpPort = { param($gateway, $port) return $false }.GetNewClosure()
        GetUrl = { param($url) return [pscustomobject]@{StatusCode=200; Content='ok'} }.GetNewClosure()
        Sleep = { param($seconds) }.GetNewClosure()
    }
    return [pscustomobject]@{State=$state; Operations=$ops}
}

function Invoke-FakeProbe($environment) {
    & $probe -ScannerSsid 'DIRECT-ES60W' -ScannerPassword 'test-only-password' -InterfaceAlias 'Wi-Fi 1' -Operations $environment.Operations
}

function Test-Case([string] $Name, [scriptblock] $Body) {
    & $Body
    $script:passed++
    Write-Output "PASS $Name"
}

Test-Case 'existing scanner profile survives and only the temporary profile is deleted' {
    $fake = New-FakeEnvironment -ExistingScannerProfile
    $before = @($fake.State.Profiles.Keys | Sort-Object)
    $null = Invoke-FakeProbe $fake
    Assert-True ((@($fake.State.Profiles.Keys | Sort-Object) -join '|') -eq ($before -join '|')) 'Existing profiles changed'
    Assert-True ($fake.State.Connections['Wi-Fi 1'].ProfileName -eq 'Home Profile') 'Original profile was not restored'
    Assert-True ($fake.State.XmlPaths.Count -eq 1 -and -not (Test-Path -LiteralPath $fake.State.XmlPaths[0])) 'Temporary XML remains'
    Assert-True (@($fake.State.Calls | Where-Object { $_ -eq 'remove:Wi-Fi 1:Epson Existing' }).Count -eq 0) 'Existing scanner profile was deleted'
}

Test-Case 'absent scanner profile leaves no profile or credential XML behind' {
    $fake = New-FakeEnvironment
    $null = Invoke-FakeProbe $fake
    Assert-True ((@($fake.State.Profiles.Keys | Sort-Object) -join '|') -eq 'Home Profile') 'Temporary scanner profile remains'
    Assert-True ($fake.State.XmlPaths.Count -eq 1 -and -not (Test-Path -LiteralPath $fake.State.XmlPaths[0])) 'Temporary XML remains'
}

Test-Case 'only the selected adapter is touched when two adapters exist' {
    $fake = New-FakeEnvironment
    $null = Invoke-FakeProbe $fake
    Assert-True ($fake.State.Connections['Wi-Fi 2'].Ssid -eq 'Office') 'Other adapter connection changed'
    Assert-True (@($fake.State.Calls | Where-Object { $_ -match 'Wi-Fi 2' }).Count -eq 0) 'Other adapter received an operation'
}

Test-Case 'scanner connect failure still restores connection and removes owned resources' {
    $fake = New-FakeEnvironment -FailScannerConnect
    $failure = $null
    try { $null = Invoke-FakeProbe $fake } catch { $failure = $_ }
    Assert-True ($null -ne $failure -and $failure.Exception.ToString().Contains('scanner connection failed')) 'Primary connection failure missing'
    Assert-True ($fake.State.Connections['Wi-Fi 1'].ProfileName -eq 'Home Profile') 'Original connection was not restored'
    Assert-True ((@($fake.State.Profiles.Keys) -join '|') -eq 'Home Profile') 'Temporary profile remains after failure'
    Assert-True ($fake.State.XmlPaths.Count -eq 1 -and -not (Test-Path -LiteralPath $fake.State.XmlPaths[0])) 'Temporary XML remains after failure'
}

Test-Case 'partial add failure removes only the owned profile and credential XML' {
    $fake = New-FakeEnvironment -ExistingScannerProfile -FailAfterAdd
    $before = @($fake.State.Profiles.Keys | Sort-Object)
    $failure = $null
    try { $null = Invoke-FakeProbe $fake } catch { $failure = $_ }
    Assert-True ($null -ne $failure -and $failure.Exception.ToString().Contains('add reported failure')) 'Partial add failure missing'
    Assert-True ((@($fake.State.Profiles.Keys | Sort-Object) -join '|') -eq ($before -join '|')) 'Owned profile remains or existing profile changed'
    Assert-True ($fake.State.Connections['Wi-Fi 1'].ProfileName -eq 'Home Profile') 'Original connection changed'
    Assert-True ($fake.State.XmlPaths.Count -eq 1 -and -not (Test-Path -LiteralPath $fake.State.XmlPaths[0])) 'Temporary XML remains after partial add'
}

Test-Case 'primary and restoration failures remain separately available' {
    $fake = New-FakeEnvironment -FailScannerConnect -FailRestore
    $failure = $null
    try { $null = Invoke-FakeProbe $fake } catch { $failure = $_ }
    Assert-True ($null -ne $failure) 'Probe unexpectedly succeeded'
    $detail = $failure.Exception.ToString()
    Assert-True ($detail.Contains('scanner connection failed')) 'Primary failure was lost'
    Assert-True ($detail.Contains('restore connection failed')) 'Restoration failure was lost'
    Assert-True ((@($fake.State.Profiles.Keys) -join '|') -eq 'Home Profile') 'Temporary profile remains after restoration failure'
    Assert-True ($fake.State.XmlPaths.Count -eq 1 -and -not (Test-Path -LiteralPath $fake.State.XmlPaths[0])) 'Temporary XML remains after restoration failure'
}

Test-Case 'default netsh wrapper parses the selected adapter and scopes profile commands' {
    $native = [pscustomobject]@{
        Calls = [System.Collections.ArrayList]::new()
        Profiles = @{'Home Profile'='Home Network'; 'Office Profile'='Office'; 'Epson Existing'='DIRECT-ES60W'}
        Connections = @{'Wi-Fi 1'=[pscustomobject]@{Ssid='Home Network'; ProfileName='Home Profile'};
                        'Wi-Fi 2'=[pscustomobject]@{Ssid='Office'; ProfileName='Office Profile'}}
    }
    $executor = {
        param([string[]] $commandArgs)
        [void] $native.Calls.Add(@($commandArgs))
        $output = @()
        $command = $commandArgs[1..2] -join ' '
        switch ($command) {
            'show interfaces' {
                # The unselected adapter appears first to catch first-SSID parsing.
                $output = @(
                    '    Name                   : Wi-Fi 2',
                    '    State                  : connected',
                    '    SSID                   : Office',
                    '    Profile                : Office Profile',
                    '',
                    '    Name                   : Wi-Fi 1',
                    '    State                  : connected',
                    "    SSID                   : $($native.Connections['Wi-Fi 1'].Ssid)",
                    "    Profile                : $($native.Connections['Wi-Fi 1'].ProfileName)"
                )
            }
            'show networks' {
                if ($commandArgs -notcontains 'interface=Wi-Fi 1') { throw 'Network scan used the wrong adapter' }
                $output = @('SSID 1 : DIRECT-ES60W')
            }
            'show profiles' {
                if ($commandArgs -notcontains 'interface=Wi-Fi 1') { throw 'Profile query used the wrong adapter' }
                $output = @($native.Profiles.Keys | ForEach-Object { "    All User Profile     : $_" })
            }
            'add profile' {
                if ($commandArgs -notcontains 'interface=Wi-Fi 1') { throw 'Profile add used the wrong adapter' }
                $pathArgument = @($commandArgs | Where-Object { $_ -like 'filename=*' })[0]
                [xml] $xml = Get-Content -LiteralPath $pathArgument.Substring(9) -Raw
                $native.Profiles[$xml.WLANProfile.name] = $xml.WLANProfile.SSIDConfig.SSID.name
            }
            'delete profile' {
                if ($commandArgs -notcontains 'interface=Wi-Fi 1') { throw 'Profile delete used the wrong adapter' }
                $profileArgument = @($commandArgs | Where-Object { $_ -like 'name=*' })[0]
                $native.Profiles.Remove($profileArgument.Substring(5))
            }
            default {
                if ($commandArgs[1] -eq 'connect') {
                    if ($commandArgs -notcontains 'interface=Wi-Fi 1') { throw 'Connect used the wrong adapter' }
                    $profileArgument = @($commandArgs | Where-Object { $_ -like 'name=*' })[0]
                    $profileName = $profileArgument.Substring(5)
                    $native.Connections['Wi-Fi 1'] = [pscustomobject]@{Ssid=$native.Profiles[$profileName]; ProfileName=$profileName}
                } else { throw "Unexpected netsh call: $($commandArgs -join ' ')" }
            }
        }
        return [pscustomobject]@{ExitCode=0; Output=$output}
    }.GetNewClosure()
    $safeOperations = @{
        GetGateway = { param($alias) return '192.0.2.1' }
        TestTcpPort = { param($gateway, $port) return $false }
        GetUrl = { param($url) return [pscustomobject]@{StatusCode=200; Content='ok'} }
        Sleep = { param($seconds) }
    }
    $null = & $probe -ScannerSsid 'DIRECT-ES60W' -ScannerPassword 'test-only-password' -InterfaceAlias 'Wi-Fi 1' -NativeNetshExecutor $executor -Operations $safeOperations
    Assert-True ($native.Connections['Wi-Fi 1'].ProfileName -eq 'Home Profile') 'Selected adapter was not restored from its own profile'
    Assert-True ($native.Connections['Wi-Fi 2'].ProfileName -eq 'Office Profile') 'Unselected adapter changed'
    Assert-True ((@($native.Profiles.Keys | Sort-Object) -join '|') -eq 'Epson Existing|Home Profile|Office Profile') 'Native profile set changed'
    $mutatingCalls = @($native.Calls | Where-Object { $_[1] -in @('add', 'connect', 'delete', 'disconnect') })
    Assert-True ($mutatingCalls.Count -ge 4 -and @($mutatingCalls | Where-Object { $_ -notcontains 'interface=Wi-Fi 1' }).Count -eq 0) 'Native mutation lacked selected interface'
}

Write-Output "PASS $passed probe tests"
