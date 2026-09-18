# Hardware acceptance check for the first firmware milestone.
$usb = Get-PnpDevice -PresentOnly | Where-Object {
    $_.InstanceId -match 'VID_303A&PID_4002' -and $_.Status -eq 'OK'
}
$serial = if ($usb) { ($usb[0].InstanceId -split '\\')[-1] } else { '' }
$device = Get-CimInstance Win32_DiskDrive | Where-Object {
    $serial -and $_.PNPDeviceID -match [regex]::Escape($serial) -and $_.Size -gt 0
}

if (-not $device) {
    Write-Error 'Scanner ESP Gateway USB mass-storage device is not enumerated.'
    exit 1
}

Write-Output "USB disk enumerated: $($device[0].Model)"
