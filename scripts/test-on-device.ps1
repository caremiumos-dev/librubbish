[CmdletBinding()]
param(
    [string]$Library = '',
    [string]$SdkRoot = ''
)

$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($SdkRoot)) {
    if (-not [string]::IsNullOrWhiteSpace($env:ANDROID_SDK_ROOT)) {
        $SdkRoot = $env:ANDROID_SDK_ROOT
    }
    elseif (-not [string]::IsNullOrWhiteSpace($env:ANDROID_HOME)) {
        $SdkRoot = $env:ANDROID_HOME
    }
    else {
        $SdkRoot = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
    }
}
if ([string]::IsNullOrWhiteSpace($Library)) {
    $Library = Join-Path $sourceRoot 'build\android-armeabi-v7a\librubbish.so'
}
if (-not (Test-Path -LiteralPath $Library)) {
    throw "Library not found: $Library. Run scripts/build.ps1 first."
}

$buildTools = Get-ChildItem -LiteralPath (Join-Path $SdkRoot 'build-tools') -Directory |
    Sort-Object { [version]$_.Name } -Descending |
    Select-Object -First 1
$platform = Get-ChildItem -LiteralPath (Join-Path $SdkRoot 'platforms') -Directory |
    Sort-Object { [version]($_.Name -replace '^android-', '') } -Descending |
    Select-Object -First 1
$androidJar = Join-Path $platform.FullName 'android.jar'

$stage = Join-Path $sourceRoot 'build\android-test'
$classes = Join-Path $stage 'classes'
$dex = Join-Path $stage 'dex'
$apkRoot = Join-Path $stage 'apk'
if (Test-Path -LiteralPath $stage) {
    $resolvedStage = (Resolve-Path -LiteralPath $stage).Path
    $resolvedSource = (Resolve-Path -LiteralPath $sourceRoot).Path
    if (-not $resolvedStage.StartsWith($resolvedSource, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace a test directory outside the source tree: $resolvedStage"
    }
    Remove-Item -LiteralPath $resolvedStage -Recurse -Force
}
New-Item -ItemType Directory -Path $classes, $dex, $apkRoot | Out-Null

$javaSources = Get-ChildItem -LiteralPath (Join-Path $sourceRoot 'tests\android\java') -Filter *.java -Recurse |
    Select-Object -ExpandProperty FullName
& javac -source 8 -target 8 -classpath $androidJar -d $classes $javaSources
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$classesJar = Join-Path $stage 'classes.jar'
& jar --create --file $classesJar -C $classes .
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& (Join-Path $buildTools.FullName 'd8.bat') --lib $androidJar --min-api 21 --output $dex $classesJar
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$unsigned = Join-Path $stage 'unsigned.apk'
& (Join-Path $buildTools.FullName 'aapt2.exe') link `
    -o $unsigned `
    -I $androidJar `
    --manifest (Join-Path $sourceRoot 'tests\android\AndroidManifest.xml') `
    --min-sdk-version 21 `
    --target-sdk-version 27
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Copy-Item -LiteralPath (Join-Path $dex 'classes.dex') -Destination (Join-Path $apkRoot 'classes.dex')
New-Item -ItemType Directory -Force -Path (Join-Path $apkRoot 'lib\armeabi-v7a') | Out-Null
Copy-Item -LiteralPath $Library -Destination (Join-Path $apkRoot 'lib\armeabi-v7a\librubbish.so')
& jar --update --file $unsigned -C $apkRoot classes.dex -C $apkRoot lib
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$aligned = Join-Path $stage 'aligned.apk'
& (Join-Path $buildTools.FullName 'zipalign.exe') -f 4 $unsigned $aligned
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$signed = Join-Path $stage 'librubbish-test.apk'
$keystore = Join-Path $env:USERPROFILE '.android\debug.keystore'
if (-not (Test-Path -LiteralPath $keystore)) {
    throw "Android debug keystore not found: $keystore"
}
& (Join-Path $buildTools.FullName 'apksigner.bat') sign `
    --ks $keystore `
    --ks-pass pass:android `
    --key-pass pass:android `
    --out $signed `
    $aligned
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& adb install -r $signed
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& adb logcat -c
& adb shell am force-stop dev.caremium.librubbish.test
& adb shell am start -W -n dev.caremium.librubbish.test/.MainActivity
Start-Sleep -Seconds 2
& adb logcat -d -s 'librubbish_test:I' 'secretapi:D' 'AndroidRuntime:E' '*:S'

$result = (& adb shell run-as dev.caremium.librubbish.test cat files/result.txt | Out-String).Trim()
Write-Host "Device result: $result"
if ($result -notmatch '^PASS ') {
    throw 'On-device test did not pass.'
}
