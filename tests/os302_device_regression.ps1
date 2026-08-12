param(
    [string]$ExpectedVersion = "v1.5.7-fix",
    [string]$CustomPackage = "com.kugou.android.lite",
    [string]$Serial = "",
    [switch]$Execute,
    [switch]$AllowAudioRestart
)

$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Case = Join-Path $Repo "tests\os302_device_case.sh"
$Remote = "/data/local/tmp/a2h_os302_device_case.sh"
$AdbArgs = @()
if ($Serial) { $AdbArgs = @("-s", $Serial) }

& adb @AdbArgs get-state | Out-Null
if ($LASTEXITCODE -ne 0) { throw "adb device unavailable" }
& adb @AdbArgs push $Case $Remote | Out-Null
if ($LASTEXITCODE -ne 0) { throw "failed to push device case" }
& adb @AdbArgs shell chmod 0755 $Remote

$ExecuteValue = if ($Execute) { "1" } else { "0" }
$RestartValue = if ($AllowAudioRestart) { "1" } else { "0" }
if ($Execute -and -not $AllowAudioRestart) {
    Write-Warning "Audio PID restart checks are skipped. Use -AllowAudioRestart for the complete matrix."
}

try {
    $Command = "A2H_EXPECTED_VERSION='$ExpectedVersion' A2H_TEST_PACKAGE='$CustomPackage' A2H_EXECUTE=$ExecuteValue A2H_RESTART_AUDIO=$RestartValue sh $Remote"
    & adb @AdbArgs shell su -c $Command
    if ($LASTEXITCODE -ne 0) { throw "OS3.0.302 device regression failed" }
}
finally {
    & adb @AdbArgs shell rm -f $Remote 2>$null
}
