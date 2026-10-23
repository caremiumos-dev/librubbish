[CmdletBinding()]
param(
    [ValidateSet('armeabi-v7a', 'arm64-v8a')]
    [string]$Abi = 'armeabi-v7a',

    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',

    [string]$NdkRoot = '',

    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($NdkRoot)) {
    if (-not [string]::IsNullOrWhiteSpace($env:ANDROID_NDK_HOME)) {
        $NdkRoot = $env:ANDROID_NDK_HOME
    }
    elseif (-not [string]::IsNullOrWhiteSpace($env:ANDROID_NDK_ROOT)) {
        $NdkRoot = $env:ANDROID_NDK_ROOT
    }
    else {
        $sdkNdkRoot = Join-Path $env:LOCALAPPDATA 'Android\Sdk\ndk'
        if (-not (Test-Path -LiteralPath $sdkNdkRoot)) {
            throw 'Android NDK not found. Set ANDROID_NDK_HOME or pass -NdkRoot.'
        }

        $NdkRoot = Get-ChildItem -LiteralPath $sdkNdkRoot -Directory |
            Where-Object { $_.Name -match '^\d+(\.\d+)+$' } |
            Sort-Object { [version]$_.Name } -Descending |
            Select-Object -First 1 -ExpandProperty FullName
    }
}

$toolchain = Join-Path $NdkRoot 'build\cmake\android.toolchain.cmake'
if (-not (Test-Path -LiteralPath $toolchain)) {
    throw "Invalid Android NDK: '$NdkRoot' (missing $toolchain)"
}

$sourceRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $sourceRoot "build\android-$Abi"

if ($Clean -and (Test-Path -LiteralPath $buildRoot)) {
    $resolvedBuild = (Resolve-Path -LiteralPath $buildRoot).Path
    $resolvedSource = (Resolve-Path -LiteralPath $sourceRoot).Path
    if (-not $resolvedBuild.StartsWith($resolvedSource, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean a build directory outside the source tree: $resolvedBuild"
    }
    Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
}

$cmakeArgs = @(
    '-S', $sourceRoot,
    '-B', $buildRoot,
    '-G', 'Ninja',
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    "-DANDROID_ABI=$Abi",
    '-DANDROID_PLATFORM=android-21',
    '-DANDROID_STL=c++_static',
    "-DCMAKE_BUILD_TYPE=$Configuration"
)

& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& cmake --build $buildRoot --config $Configuration
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$library = Join-Path $buildRoot 'librubbish.so'
if (-not (Test-Path -LiteralPath $library)) {
    throw "Build completed but $library was not produced."
}

$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $library).Hash
Write-Host "Built: $library"
Write-Host "SHA-256: $hash"
