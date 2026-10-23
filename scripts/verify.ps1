[CmdletBinding()]
param(
    [string]$Library = '',
    [ValidateSet('armeabi-v7a', 'arm64-v8a')]
    [string]$Abi = 'armeabi-v7a'
)

$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($Library)) {
    $Library = Join-Path $sourceRoot "build\android-$Abi\librubbish.so"
}
if (-not (Test-Path -LiteralPath $Library)) {
    throw "Library not found: $Library"
}

$readelf = Get-Command llvm-readelf.exe -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty Source
$nm = Get-Command llvm-nm.exe -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty Source
if (-not $readelf -or -not $nm) {
    throw 'llvm-readelf.exe and llvm-nm.exe must be on PATH.'
}

$header = (& $readelf -h $Library | Out-String)
$dynamic = (& $readelf -d -W $Library | Out-String)
$symbolNames = & $nm -D --defined-only $Library |
    ForEach-Object { ($_ -split '\s+')[-1] }

$expectedMachine = if ($Abi -eq 'armeabi-v7a') { 'ARM' } else { 'AArch64' }
if ($header -notmatch "Machine:\s+$expectedMachine") {
    throw "Unexpected ELF machine for $Abi."
}
if ($dynamic -notmatch 'Library soname: \[librubbish\.so\]') {
    throw 'Unexpected SONAME.'
}

$requiredSymbols = @(
    'JNI_OnLoad',
    'Java_com_xtc_secretapi_XTCGetUtilWrapper_init',
    'getApplication',
    'getSignArray',
    'get6',
    'jstringToChar',
    'AUTH_KEY',
    'ERROR_KEY'
)

foreach ($symbol in $requiredSymbols) {
    if ($symbolNames -notcontains $symbol) {
        throw "Missing exported symbol: $symbol"
    }
}

$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Library).Hash
Write-Host "Verified ABI: $Abi"
Write-Host 'SONAME: librubbish.so'
Write-Host "Exports: $($requiredSymbols -join ', ')"
Write-Host "SHA-256: $hash"
