[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [ValidatePattern('^[A-Za-z]$')]
    [string]$DriveLetter = 'S',

    [switch]$PrepareUserSession
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$LegacyWebDavRemote = '\\192.168.77.1@80\DavWWWRoot'
$WebClientRegistryPath = 'HKLM:\SYSTEM\CurrentControlSet\Services\WebClient\Parameters'
$WebClientValueName = 'FileSizeLimitInBytes'
$MscHardwareId = 'VID_303A&PID_4002'

function Get-NetUseRemotePath {
    param(
        [Parameter(Mandatory)]
        [AllowEmptyCollection()]
        [AllowEmptyString()]
        [string[]]$OutputLines
    )

    $matches = [regex]::Matches(($OutputLines -join "`n"), '\\\\[^\s]+')
    $paths = @($matches | ForEach-Object { $_.Value.TrimEnd() } | Select-Object -Unique)
    if ($paths.Count -gt 1) {
        throw "Could not identify one remote path in the net use output: $($paths -join ', ')"
    }

    if ($paths.Count -eq 1) {
        return $paths[0]
    }

    return $null
}

function Get-NetUseMapping {
    param(
        [Parameter(Mandatory)]
        [char]$Letter
    )

    $netExe = Join-Path $env:SystemRoot 'System32\net.exe'
    $previousErrorAction = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = @(& $netExe use "$Letter`:" 2>&1 | ForEach-Object { "$_" })
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorAction
    }
    $remotePath = Get-NetUseRemotePath -OutputLines $output

    if ($exitCode -eq 0 -and -not $remotePath) {
        throw "net use reported a mapping for $Letter`: but its remote path could not be determined."
    }
    if ($exitCode -ne 0 -and $remotePath) {
        throw "net use returned exit code $exitCode while reporting remote path '$remotePath'."
    }
    if ($exitCode -ne 0 -and ($output -join ' ') -notmatch
        'The network connection could not be found\.') {
        throw "net use failed to inspect $Letter`: (exit $exitCode): $($output -join ' ')"
    }

    [pscustomobject]@{
        Exists     = ($exitCode -eq 0)
        RemotePath = $remotePath
        Output     = $output
    }
}

function Read-WebClientBackup {
    param(
        [Parameter(Mandatory)]
        [string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "WebClient settings backup was not found: $Path"
    }

    try {
        $raw = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    } catch {
        throw "WebClient settings backup is not valid JSON: $Path. $($_.Exception.Message)"
    }

    $propertyNames = @($raw.PSObject.Properties.Name)
    if ($propertyNames -notcontains 'existed' -or $raw.existed -isnot [bool]) {
        throw "WebClient settings backup must contain a Boolean 'existed' field: $Path"
    }

    if (-not $raw.existed) {
        return [pscustomobject]@{ Existed = $false; Value = $null }
    }

    if ($propertyNames -notcontains 'value' -or [string]$raw.value -notmatch '^\d+$') {
        throw "WebClient settings backup must contain a DWORD 'value' when 'existed' is true: $Path"
    }

    $wideValue = [uint64]([string]$raw.value)
    if ($wideValue -gt [uint32]::MaxValue) {
        throw "The backed-up WebClient value is outside the DWORD range: $wideValue"
    }

    [pscustomobject]@{ Existed = $true; Value = [uint32]$wideValue }
}

function Test-SamePartition {
    param(
        [Parameter(Mandatory)]$Left,
        [Parameter(Mandatory)]$Right
    )

    return ([uint32]$Left.DiskNumber -eq [uint32]$Right.DiskNumber -and
        [uint32]$Left.PartitionNumber -eq [uint32]$Right.PartitionNumber)
}

function Test-MscDiskCandidate {
    param(
        [Parameter(Mandatory)]$Disk,
        [Parameter(Mandatory)][string[]]$AncestorIds,
        [Parameter(Mandatory)][string[]]$DeviceIds
    )

    if ($Disk.InterfaceType -ne 'USB' -or $Disk.Size -le 0) {
        return $false
    }

    foreach ($ancestorId in $AncestorIds) {
        foreach ($deviceId in $DeviceIds) {
            if ([StringComparer]::OrdinalIgnoreCase.Equals($ancestorId, $deviceId)) {
                return $true
            }
        }
    }
    return $false
}

function Get-PnpAncestorIds {
    param([Parameter(Mandatory)][string]$InstanceId)

    $current = $InstanceId
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    for ($depth = 0; $depth -lt 32; $depth++) {
        if (-not $seen.Add($current)) {
            throw "PnP parent cycle while resolving '$InstanceId'."
        }

        $property = Get-PnpDeviceProperty -InstanceId $current -KeyName 'DEVPKEY_Device_Parent' `
            -WhatIf:$false -ErrorAction Stop
        if ($null -eq $property -or [string]::IsNullOrWhiteSpace([string]$property.Data)) {
            return
        }

        $current = [string]$property.Data
        Write-Output $current
        if ($current -match '^HTREE\\ROOT\\') {
            return
        }
    }
    throw "PnP parent chain exceeded 32 levels for '$InstanceId'."
}

function Get-MscPartition {
    # PnP property queries honor the caller's WhatIf preference even though
    # they only read state, which hides the disk during a migration preview.
    $WhatIfPreference = $false
    $devices = @(Get-PnpDevice -PresentOnly -ErrorAction Stop | Where-Object {
        $_.InstanceId -match [regex]::Escape($MscHardwareId) -and $_.Status -eq 'OK'
    })
    if ($devices.Count -eq 0) {
        return $null
    }

    $deviceIds = @($devices | ForEach-Object { $_.InstanceId } | Select-Object -Unique)

    $disks = @(Get-CimInstance Win32_DiskDrive -ErrorAction Stop | Where-Object {
        if ($_.InterfaceType -ne 'USB' -or $_.Size -le 0) {
            return $false
        }
        $ancestors = @($_.PNPDeviceID) + @(Get-PnpAncestorIds -InstanceId $_.PNPDeviceID)
        Test-MscDiskCandidate -Disk $_ -AncestorIds $ancestors -DeviceIds $deviceIds
    })

    if ($disks.Count -eq 0) {
        return $null
    }
    if ($disks.Count -ne 1) {
        throw "Found $($disks.Count) disks matching $MscHardwareId; refusing to choose one."
    }

    $disk = $disks[0]
    $partitions = @(Get-Partition -DiskNumber ([uint32]$disk.Index) -ErrorAction Stop | Where-Object {
        $_.Size -gt 0 -and $_.Type -notin @('Reserved', 'System')
    })
    if ($partitions.Count -ne 1) {
        throw "Expected one usable partition on MSC disk $($disk.Index), found $($partitions.Count)."
    }

    [pscustomobject]@{
        Disk      = $disk
        Partition = $partitions[0]
    }
}

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Test-UacEnabled {
    $policy = Get-ItemProperty -LiteralPath `
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System' `
        -Name EnableLUA -ErrorAction Stop
    return ([uint32]$policy.EnableLUA -ne 0)
}

function Get-PreparationPath {
    if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        throw 'LOCALAPPDATA is unavailable; cannot verify the Explorer logon session.'
    }
    return (Join-Path $env:LOCALAPPDATA 'ScannerEspGateway\usb-msc-user-session.json')
}

function Test-PreparationRecord {
    param(
        [Parameter(Mandatory)]$Record,
        [Parameter(Mandatory)][string]$UserSid,
        [Parameter(Mandatory)][string]$DriveLetter,
        [Parameter(Mandatory)][string]$DiskPnpId,
        [Parameter(Mandatory)][datetime]$NowUtc
    )

    try {
        $preparedUtc = [datetime]::Parse(
            [string]$Record.PreparedUtc,
            [Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::RoundtripKind
        ).ToUniversalTime()
    } catch {
        return $false
    }

    return (
        [StringComparer]::Ordinal.Equals([string]$Record.UserSid, $UserSid) -and
        [StringComparer]::OrdinalIgnoreCase.Equals([string]$Record.DriveLetter, $DriveLetter) -and
        [StringComparer]::OrdinalIgnoreCase.Equals([string]$Record.DiskPnpId, $DiskPnpId) -and
        $preparedUtc -le $NowUtc -and
        ($NowUtc - $preparedUtc).TotalMinutes -le 10
    )
}

function Assert-UserSessionPrepared {
    param([Parameter(Mandatory)]$Msc, [Parameter(Mandatory)][char]$Letter)

    $path = Get-PreparationPath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Explorer session has not been prepared. Run this script without elevation with -PrepareUserSession -DriveLetter $Letter first."
    }

    try {
        $record = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    } catch {
        throw "Explorer session preparation record is invalid: $path"
    }

    $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    if (-not (Test-PreparationRecord -Record $record -UserSid $sid -DriveLetter ([string]$Letter) `
        -DiskPnpId ([string]$Msc.Disk.PNPDeviceID) -NowUtc ([datetime]::UtcNow))) {
        throw "Explorer session preparation is stale or belongs to another user, drive letter, or MSC device. Run -PrepareUserSession again."
    }

    $persistentMapping = Get-ItemProperty -LiteralPath 'HKCU:\Network\S' -Name RemotePath `
        -ErrorAction SilentlyContinue
    if ($null -ne $persistentMapping) {
        throw "A persistent S: mapping remains in this user's profile; remove it from the unelevated Explorer session and prepare again."
    }
}

function Get-RegistryValueState {
    $item = Get-ItemProperty -LiteralPath $WebClientRegistryPath -Name $WebClientValueName `
        -ErrorAction SilentlyContinue
    if ($null -eq $item) {
        return [pscustomobject]@{ Exists = $false; Value = $null }
    }

    [pscustomobject]@{
        Exists = $true
        Value  = $item.$WebClientValueName
    }
}

function Stage-PreparationRecord {
    param(
        [Parameter(Mandatory)][string]$RecordPath,
        [Parameter(Mandatory)][string]$StagePath,
        [Parameter(Mandatory)]$Record
    )

    [void](New-Item -ItemType Directory -Path (Split-Path $RecordPath -Parent) -Force)
    $Record | ConvertTo-Json | Set-Content -LiteralPath $StagePath -Encoding UTF8 -ErrorAction Stop
    [void](Get-Content -LiteralPath $StagePath -Raw -ErrorAction Stop | ConvertFrom-Json)
}

function Publish-PreparationRecord {
    param(
        [Parameter(Mandatory)][string]$RecordPath,
        [Parameter(Mandatory)][string]$StagePath
    )

    if (Test-Path -LiteralPath $RecordPath) {
        $backupPath = "$RecordPath.previous-$([guid]::NewGuid().ToString('N'))"
        [IO.File]::Replace($StagePath, $RecordPath, $backupPath)
        Remove-Item -LiteralPath $backupPath -Force -ErrorAction SilentlyContinue
    } else {
        [IO.File]::Move($StagePath, $RecordPath)
    }
}

function Discard-PreparationStage {
    param([Parameter(Mandatory)][string]$StagePath)

    if (Test-Path -LiteralPath $StagePath) {
        Remove-Item -LiteralPath $StagePath -Force -ErrorAction Stop
    }
}

function Invoke-PreparationTransaction {
    param(
        [Parameter(Mandatory)][bool]$MappingExists,
        [Parameter(Mandatory)][scriptblock]$ApproveMarker,
        [Parameter(Mandatory)][scriptblock]$ApproveRemoval,
        [Parameter(Mandatory)][scriptblock]$StageMarker,
        [Parameter(Mandatory)][scriptblock]$RemoveMapping,
        [Parameter(Mandatory)][scriptblock]$PublishMarker,
        [Parameter(Mandatory)][scriptblock]$DiscardMarker,
        [Parameter(Mandatory)][scriptblock]$InvalidateMarker,
        [Parameter(Mandatory)][scriptblock]$RollbackMapping
    )

    if (-not (& $ApproveMarker)) {
        throw 'Explorer session preparation was declined; S: was not changed.'
    }
    if ($MappingExists -and -not (& $ApproveRemoval)) {
        throw 'Legacy S: mapping removal was declined; S: was not changed.'
    }

    $mappingRemovalAttempted = $false
    try {
        & $StageMarker | Out-Null
        if ($MappingExists) {
            $mappingRemovalAttempted = $true
            & $RemoveMapping | Out-Null
        }
        & $PublishMarker | Out-Null
    } catch {
        $originalError = $_
        $cleanupErrors = [Collections.Generic.List[string]]::new()
        try { & $DiscardMarker | Out-Null } catch { $cleanupErrors.Add("marker cleanup: $($_.Exception.Message)") }
        if ($mappingRemovalAttempted) {
            try { & $InvalidateMarker | Out-Null } catch { $cleanupErrors.Add("marker invalidation: $($_.Exception.Message)") }
            try { & $RollbackMapping | Out-Null } catch { $cleanupErrors.Add("S: rollback: $($_.Exception.Message)") }
        }
        if ($cleanupErrors.Count -gt 0) {
            throw "Preparation failed: $($originalError.Exception.Message). Additional failures: $($cleanupErrors -join '; ')"
        }
        throw $originalError
    }
}

function Remove-ExactLegacyMapping {
    $currentMapping = Get-NetUseMapping -Letter ([char]'S')
    if (-not $currentMapping.Exists -or
        -not [StringComparer]::OrdinalIgnoreCase.Equals($currentMapping.RemotePath, $LegacyWebDavRemote)) {
        throw 'S: changed after the safety check; refusing to remove it.'
    }

    $netExe = Join-Path $env:SystemRoot 'System32\net.exe'
    $deleteOutput = @(& $netExe use 'S:' /delete /y 2>&1 | ForEach-Object { "$_" })
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to remove the exact legacy mapping for S:. $($deleteOutput -join ' ')"
    }
}

function Restore-ExactLegacyMapping {
    param([Parameter(Mandatory)][bool]$WasPersistent)

    $currentMapping = Get-NetUseMapping -Letter ([char]'S')
    if ($currentMapping.Exists) {
        if (-not [StringComparer]::OrdinalIgnoreCase.Equals($currentMapping.RemotePath, $LegacyWebDavRemote)) {
            throw "S: was taken by '$($currentMapping.RemotePath)'; rollback will not replace it."
        }
        return
    }

    if (@(Get-Partition -DriveLetter S -ErrorAction SilentlyContinue).Count -gt 0) {
        throw 'S: became occupied by a local volume; rollback will not replace it.'
    }
    $psDrive = Get-PSDrive -Name S -ErrorAction SilentlyContinue
    if ($null -ne $psDrive -and
        -not [StringComparer]::OrdinalIgnoreCase.Equals([string]$psDrive.DisplayRoot, $LegacyWebDavRemote) -and
        -not ($psDrive.Provider.Name -eq 'FileSystem' -and $psDrive.Root -eq 'S:\')) {
        throw 'S: became occupied by another provider drive; rollback will not replace it.'
    }

    $persistence = if ($WasPersistent) { '/persistent:yes' } else { '/persistent:no' }
    $netExe = Join-Path $env:SystemRoot 'System32\net.exe'
    $restoreOutput = @(& $netExe use 'S:' $LegacyWebDavRemote $persistence 2>&1 | ForEach-Object { "$_" })
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to restore legacy S: mapping: $($restoreOutput -join ' ')"
    }
}

function Invoke-Migration {
    [CmdletBinding(SupportsShouldProcess)]
    param(
        [Parameter(Mandatory)]
        [bool]$DryRun
    )

    $letter = [char]$DriveLetter.ToUpperInvariant()
    $backupPath = Join-Path (Split-Path $PSScriptRoot -Parent) 'backups\webclient-settings.json'

    # Complete all safety checks that can fail before making any change.
    $backup = Read-WebClientBackup -Path $backupPath
    if ($backup.Existed -and -not (Test-Path -LiteralPath $WebClientRegistryPath)) {
        throw "WebClient registry key does not exist: $WebClientRegistryPath"
    }

    $legacyMapping = Get-NetUseMapping -Letter ([char]'S')
    if ($legacyMapping.Exists -and
        -not [StringComparer]::OrdinalIgnoreCase.Equals($legacyMapping.RemotePath, $LegacyWebDavRemote)) {
        throw "S: is mapped to '$($legacyMapping.RemotePath)'. Refusing to replace it."
    }

    $targetMapping = if ($letter -eq 'S') { $legacyMapping } else { Get-NetUseMapping -Letter $letter }
    if ($letter -ne 'S' -and $targetMapping.Exists) {
        throw "$letter`: is mapped to '$($targetMapping.RemotePath)'. Refusing to replace it."
    }

    $msc = Get-MscPartition
    if ($null -eq $msc -and -not $DryRun) {
        throw "Scanner ESP Gateway MSC device $MscHardwareId is not present; no mapping or registry setting was changed."
    }

    $localPartitions = @(Get-Partition -DriveLetter $letter -ErrorAction SilentlyContinue)
    if ($localPartitions.Count -gt 1) {
        throw "Multiple local partitions report drive letter $letter`; refusing to continue."
    }

    $sameMscVolume = ($msc -and $localPartitions.Count -eq 1 -and
        (Test-SamePartition -Left $msc.Partition -Right $localPartitions[0]))

    if (-not $targetMapping.Exists -and $localPartitions.Count -eq 1 -and -not $sameMscVolume) {
        throw "$letter`: is assigned to local disk $($localPartitions[0].DiskNumber), partition $($localPartitions[0].PartitionNumber). Refusing to replace it."
    }

    $psDrive = Get-PSDrive -Name $letter -ErrorAction SilentlyContinue
    if (-not $targetMapping.Exists -and $localPartitions.Count -eq 0 -and $null -ne $psDrive) {
        throw "$letter`: is occupied by provider '$($psDrive.Provider.Name)' at '$($psDrive.Root)'. Refusing to replace it."
    }

    if (-not $DryRun) {
        if ($PrepareUserSession) {
            if ((Test-IsAdministrator) -and (Test-UacEnabled)) {
                throw 'Run -PrepareUserSession from the unelevated PowerShell session that shares mappings with Explorer while UAC is enabled.'
            }
        } else {
            if (-not (Test-IsAdministrator)) {
                throw 'Run -PrepareUserSession first, then run this migration from an elevated PowerShell session.'
            }
            if (-not ($sameMscVolume -and $letter -eq 'S' -and -not $legacyMapping.Exists)) {
                Assert-UserSessionPrepared -Msc $msc -Letter $letter
            }
        }
    }

    if ($PrepareUserSession) {
        if ($DryRun) {
            if ($legacyMapping.Exists) {
                [void]$PSCmdlet.ShouldProcess("S: -> $($legacyMapping.RemotePath)", 'Remove exact legacy WebDAV mapping')
            }
            if ($null -eq $msc) {
                Write-Warning "Scanner ESP Gateway MSC device $MscHardwareId is not present; Explorer session preparation was skipped."
            } else {
                [void]$PSCmdlet.ShouldProcess((Get-PreparationPath), 'Record verified Explorer session preparation')
                Write-Output 'WhatIf: Explorer session preparation would be recorded after legacy S: cleanup.'
            }
            return
        }

        $persistentMapping = Get-ItemProperty -LiteralPath 'HKCU:\Network\S' -Name RemotePath `
            -ErrorAction SilentlyContinue
        $wasPersistent = ($null -ne $persistentMapping)

        $recordPath = Get-PreparationPath
        $recordDirectory = Split-Path $recordPath -Parent
        $stagePath = Join-Path $recordDirectory ([IO.Path]::GetRandomFileName())
        $record = [ordered]@{
            UserSid     = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
            DriveLetter = [string]$letter
            DiskPnpId   = [string]$msc.Disk.PNPDeviceID
            PreparedUtc = [datetime]::UtcNow.ToString('o')
        }
        $markerApproved = $PSCmdlet.ShouldProcess(
            $recordPath, 'Record verified Explorer session preparation')
        $removalApproved = (-not $legacyMapping.Exists) -or $PSCmdlet.ShouldProcess(
            "S: -> $LegacyWebDavRemote", 'Remove exact legacy WebDAV mapping')
        $approveMarker = { $markerApproved }.GetNewClosure()
        $approveRemoval = { $removalApproved }.GetNewClosure()
        $stageMarker = {
            Stage-PreparationRecord -RecordPath $recordPath -StagePath $stagePath -Record $record
        }.GetNewClosure()
        $removeMapping = { Remove-ExactLegacyMapping }
        $publishMarker = {
            if ((Get-NetUseMapping -Letter ([char]'S')).Exists) {
                throw 'S: is still mapped in the Explorer logon session.'
            }
            if ($null -ne (Get-ItemProperty -LiteralPath 'HKCU:\Network\S' -Name RemotePath -ErrorAction SilentlyContinue)) {
                throw 'A persistent S: mapping remains in the Explorer profile.'
            }
            Publish-PreparationRecord -RecordPath $recordPath -StagePath $stagePath
        }.GetNewClosure()
        $discardMarker = {
            Discard-PreparationStage -StagePath $stagePath
        }.GetNewClosure()
        $invalidateMarker = {
            if (Test-Path -LiteralPath $recordPath) {
                Remove-Item -LiteralPath $recordPath -Force -ErrorAction Stop
            }
        }.GetNewClosure()
        $rollbackMapping = { Restore-ExactLegacyMapping -WasPersistent $wasPersistent }.GetNewClosure()

        Invoke-PreparationTransaction -MappingExists $legacyMapping.Exists `
            -ApproveMarker $approveMarker -ApproveRemoval $approveRemoval -StageMarker $stageMarker `
            -RemoveMapping $removeMapping -PublishMarker $publishMarker `
            -DiscardMarker $discardMarker -InvalidateMarker $invalidateMarker `
            -RollbackMapping $rollbackMapping
        Write-Output "Explorer session is prepared for $letter`:. Run the migration from an elevated PowerShell session within ten minutes."
        return
    }

    if ($legacyMapping.Exists) {
        $mappingTarget = "S: -> $($legacyMapping.RemotePath)"
        if ($PSCmdlet.ShouldProcess($mappingTarget, 'Remove exact legacy WebDAV mapping')) {
            Remove-ExactLegacyMapping
        } elseif (-not $DryRun) {
            throw 'Legacy S: mapping removal was declined; migration stopped before registry or drive-letter changes.'
        }
    }

    $registryState = Get-RegistryValueState
    if ($backup.Existed) {
        if (-not $registryState.Exists -or [uint32]$registryState.Value -ne $backup.Value) {
            $registryTarget = "$WebClientRegistryPath\$WebClientValueName"
            if ($PSCmdlet.ShouldProcess($registryTarget, "Restore backed-up DWORD value $($backup.Value)")) {
                New-ItemProperty -LiteralPath $WebClientRegistryPath -Name $WebClientValueName `
                    -PropertyType DWord -Value $backup.Value -Force | Out-Null
            }
        } else {
            Write-Output "WebClient $WebClientValueName already matches the backed-up value."
        }
    } elseif ($registryState.Exists) {
        $registryTarget = "$WebClientRegistryPath\$WebClientValueName"
        if ($PSCmdlet.ShouldProcess($registryTarget, 'Remove registry value because it did not exist in the backup')) {
            Remove-ItemProperty -LiteralPath $WebClientRegistryPath -Name $WebClientValueName
        }
    } else {
        Write-Output "WebClient $WebClientValueName is already absent, matching the backup."
    }

    if ($null -eq $msc) {
        Write-Warning "Scanner ESP Gateway MSC device $MscHardwareId is not present; drive-letter assignment was skipped."
        return
    }

    if ($sameMscVolume) {
        Write-Output "Scanner ESP Gateway MSC volume already owns $letter`:."
        return
    }

    if (-not $DryRun) {
        $newMapping = Get-NetUseMapping -Letter $letter
        $newPartition = @(Get-Partition -DriveLetter $letter -ErrorAction SilentlyContinue)
        $newPsDrive = Get-PSDrive -Name $letter -ErrorAction SilentlyContinue
        $psDriveBlocks = ($null -ne $newPsDrive -and
            -not ($letter -eq 'S' -and $legacyMapping.Exists -and
                [StringComparer]::OrdinalIgnoreCase.Equals([string]$newPsDrive.DisplayRoot, $LegacyWebDavRemote)))
        if ($newMapping.Exists -or $newPartition.Count -gt 0 -or $psDriveBlocks) {
            throw "$letter`: became occupied after the safety check; refusing to assign it."
        }
    }

    $partitionTarget = "disk $($msc.Partition.DiskNumber), partition $($msc.Partition.PartitionNumber)"
    if ($PSCmdlet.ShouldProcess($partitionTarget, "Assign drive letter $letter`: to the Scanner ESP Gateway MSC volume")) {
        Set-Partition -InputObject $msc.Partition -NewDriveLetter $letter -ErrorAction Stop
        $assigned = @(Get-Partition -DriveLetter $letter -ErrorAction Stop)
        if ($assigned.Count -ne 1 -or -not (Test-SamePartition -Left $msc.Partition -Right $assigned[0])) {
            throw "Drive-letter assignment completed without $letter`: resolving to the expected MSC partition."
        }
        Write-Output "Assigned Scanner ESP Gateway MSC volume to $letter`:."
    }
}

if ($MyInvocation.InvocationName -ne '.') {
    Invoke-Migration -DryRun ([bool]$WhatIfPreference)
}
