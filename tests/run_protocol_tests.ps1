param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$binary = Join-Path $env:TEMP ('esci-test-' + [guid]::NewGuid().ToString('N') + '.exe')
try {
    & $Python -m ziglang cc -std=c11 -Wall -Wextra -Werror -I (Join-Path $root 'main') (Join-Path $PSScriptRoot 'test_esci_scan.c') (Join-Path $root 'main\esci_scan.c') -o $binary
    if ($LASTEXITCODE -ne 0) { throw 'Protocol test compilation failed' }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw 'Protocol tests failed' }
} finally {
    Remove-Item -LiteralPath $binary -Force -ErrorAction SilentlyContinue
}
