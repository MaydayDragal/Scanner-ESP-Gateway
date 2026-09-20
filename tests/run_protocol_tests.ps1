param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$binary = Join-Path $env:TEMP ('esci-test-' + [guid]::NewGuid().ToString('N') + '.exe')
$jpegFixture = Join-Path $env:TEMP ('jpeg-crop-' + [guid]::NewGuid().ToString('N') + '.jpg')
try {
    & $Python -m ziglang cc -std=c11 -Wall -Wextra -Werror -I (Join-Path $root 'main') (Join-Path $PSScriptRoot 'test_esci_scan.c') (Join-Path $root 'main\esci_scan.c') -o $binary
    if ($LASTEXITCODE -ne 0) { throw 'Protocol test compilation failed' }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw 'Protocol tests failed' }
    & $Python -m ziglang cc -std=c11 -Wall -Wextra -Werror -I (Join-Path $root 'main') (Join-Path $PSScriptRoot 'test_jpeg_crop.c') (Join-Path $root 'main\jpeg_crop.c') -o $binary
    if ($LASTEXITCODE -ne 0) { throw 'JPEG crop test compilation failed' }
    & $binary $jpegFixture
    if ($LASTEXITCODE -ne 0) { throw 'JPEG crop tests failed' }
    & $Python -m ziglang cc -std=c11 -Wall -Wextra -Werror -I (Join-Path $root 'main') (Join-Path $PSScriptRoot 'test_scanner_display_model.c') (Join-Path $root 'main\scanner_display_model.c') -o $binary
    if ($LASTEXITCODE -ne 0) { throw 'Display model test compilation failed' }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw 'Display model tests failed' }
    & $Python -m ziglang cc -std=c11 -Wall -Wextra -Werror -I (Join-Path $root 'main') (Join-Path $PSScriptRoot 'test_scanner_idle_model.c') (Join-Path $root 'main\scanner_idle_model.c') -o $binary
    if ($LASTEXITCODE -ne 0) { throw 'Idle timer test compilation failed' }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw 'Idle timer tests failed' }
    & $Python -m ziglang cc -std=c11 -Wall -Wextra -Werror -I (Join-Path $root 'main') (Join-Path $PSScriptRoot 'test_scanner_led_model.c') (Join-Path $root 'main\scanner_led_model.c') -o $binary
    if ($LASTEXITCODE -ne 0) { throw 'LED model test compilation failed' }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw 'LED model tests failed' }
    & $Python -m ziglang cc -std=c11 -Wall -Wextra -Werror -I (Join-Path $root 'main') (Join-Path $PSScriptRoot 'test_storage_handoff_model.c') (Join-Path $root 'main\storage_handoff_model.c') -o $binary
    if ($LASTEXITCODE -ne 0) { throw 'Storage handoff model test compilation failed' }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw 'Storage handoff model tests failed' }
    & $Python (Join-Path $PSScriptRoot 'run_usb_storage_tests.py')
    if ($LASTEXITCODE -ne 0) { throw 'USB storage lifecycle tests failed' }
    & $Python (Join-Path $PSScriptRoot 'test_verify_scan.py')
    if ($LASTEXITCODE -ne 0) { throw 'Scan image dimension tests failed' }
    & $Python (Join-Path $PSScriptRoot 'test_jpeg_width_crop.py')
    if ($LASTEXITCODE -ne 0) { throw 'JPEG width crop tests failed' }
} finally {
    Remove-Item -LiteralPath $binary -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $jpegFixture -Force -ErrorAction SilentlyContinue
}
