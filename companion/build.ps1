param(
    [string]$SdkRoot = (Join-Path $PSScriptRoot '..\..\.tools\android-sdk'),
    [string]$JavaHome = 'D:\Program Files\Java\jdk-21',
    [string]$SigningProperties = (Join-Path $PSScriptRoot '..\..\.tools\a2h-signing\signing.properties')
)

$ErrorActionPreference = 'Stop'
$VersionName = '1.5.7-fix'
$VersionCode = '1571'
$Platform = Join-Path $SdkRoot 'platforms\android-36\android.jar'
$BuildTools = Join-Path $SdkRoot 'build-tools\36.0.0'
$BuildDir = Join-Path $PSScriptRoot 'build'
$ClassesDir = Join-Path $BuildDir 'classes'
$DexDir = Join-Path $BuildDir 'dex'
$AssetsDir = Join-Path $BuildDir 'assets'
$ResZip = Join-Path $BuildDir 'resources.zip'
$UnsignedApk = Join-Path $BuildDir 'unsigned.apk'
$AlignedApk = Join-Path $BuildDir 'aligned.apk'
$OutputApk = Join-Path $PSScriptRoot 'a2h_companion.apk'
$ArchiveTimestamp = '1980-01-01T00:00:02Z'

function Assert-File([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required file is missing: $Path"
    }
}

function Invoke-Checked([string]$Program, [string[]]$Arguments, [string]$WorkingDirectory = $PSScriptRoot) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed ($LASTEXITCODE): $Program"
    }
}

Assert-File $Platform
$Aapt2 = Join-Path $BuildTools 'aapt2.exe'
$D8 = Join-Path $BuildTools 'd8.bat'
$ZipAlign = Join-Path $BuildTools 'zipalign.exe'
$ApkSigner = Join-Path $BuildTools 'apksigner.bat'
$Javac = Join-Path $JavaHome 'bin\javac.exe'
$Jar = Join-Path $JavaHome 'bin\jar.exe'
foreach ($Tool in @($Aapt2, $D8, $ZipAlign, $ApkSigner, $Javac, $Jar, $SigningProperties)) {
    Assert-File $Tool
}

$Signing = @{}
foreach ($Line in Get-Content -LiteralPath $SigningProperties -Encoding UTF8) {
    if ($Line -match '^([^#=]+)=(.*)$') {
        $Signing[$Matches[1].Trim()] = $Matches[2].Trim()
    }
}
foreach ($Key in @('storeFile', 'storePassword', 'keyAlias', 'keyPassword')) {
    if ([string]::IsNullOrWhiteSpace($Signing[$Key])) {
        throw "Signing property is missing: $Key"
    }
}
Assert-File $Signing.storeFile

if (Test-Path -LiteralPath $BuildDir) {
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}
New-Item -ItemType Directory -Path $ClassesDir, $DexDir, $AssetsDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\webroot\index.html') -Destination $AssetsDir
foreach ($Asset in @(
    'coolapk.webp',
    'donate-wechat-pay.webp', 'donate-wechat.webp', 'donate-alipay.webp',
    'payment-wechat-pay.webp', 'payment-wechat-reward.webp', 'payment-alipay.webp'
)) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "..\webroot\$Asset") -Destination $AssetsDir
}

Invoke-Checked $Aapt2 @(
    'compile', '--dir', (Join-Path $PSScriptRoot 'app\src\main\res'), '-o', $ResZip
)
Invoke-Checked $Aapt2 @(
    'link', '-o', $UnsignedApk, '-I', $Platform,
    '--manifest', (Join-Path $PSScriptRoot 'app\src\main\AndroidManifest.xml'),
    '--min-sdk-version', '29', '--target-sdk-version', '35',
    '--version-code', $VersionCode, '--version-name', $VersionName,
    '--auto-add-overlay', '-A', $AssetsDir, $ResZip
)

$Sources = Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'app\src\main\java') -Recurse -Filter '*.java' |
    Sort-Object FullName | ForEach-Object FullName
if (-not $Sources) { throw 'No Java sources found' }
Invoke-Checked $Javac (@(
    '-encoding', 'UTF-8', '--release', '17',
    '-classpath', $Platform, '-d', $ClassesDir
) + $Sources)
Invoke-Checked $Jar @(
    '--create', '--file', (Join-Path $BuildDir 'classes.jar'),
    "--date=$ArchiveTimestamp", '-C', $ClassesDir, '.'
)
Invoke-Checked $D8 @(
    '--lib', $Platform, '--min-api', '29', '--output', $DexDir,
    (Join-Path $BuildDir 'classes.jar')
)
Invoke-Checked $Jar @(
    '--update', '--file', $UnsignedApk,
    "--date=$ArchiveTimestamp", '-C', $DexDir, 'classes.dex'
)
Invoke-Checked $ZipAlign @('-f', '-p', '4', $UnsignedApk, $AlignedApk)

$env:A2H_STORE_PASSWORD = $Signing.storePassword
$env:A2H_KEY_PASSWORD = $Signing.keyPassword
try {
    Invoke-Checked $ApkSigner @(
        'sign', '--ks', $Signing.storeFile, '--ks-key-alias', $Signing.keyAlias,
        '--ks-pass', 'env:A2H_STORE_PASSWORD', '--key-pass', 'env:A2H_KEY_PASSWORD',
        '--out', $OutputApk, $AlignedApk
    )
} finally {
    Remove-Item Env:A2H_STORE_PASSWORD -ErrorAction SilentlyContinue
    Remove-Item Env:A2H_KEY_PASSWORD -ErrorAction SilentlyContinue
}
Invoke-Checked $ApkSigner @('verify', '--verbose', '--print-certs', $OutputApk)

$Hash = (Get-FileHash -LiteralPath $OutputApk -Algorithm SHA256).Hash
Write-Output "APK=$OutputApk"
Write-Output "SHA256=$Hash"
