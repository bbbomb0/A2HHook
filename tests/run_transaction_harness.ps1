param(
    [string]$Ndk = "D:\Download\android-ndk-r26d-windows\android-ndk-r26d",
    [string]$Serial = ""
)

$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Compiler = Join-Path $Ndk "toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android31-clang.cmd"
$Output = Join-Path $env:TEMP "a2h_patcher_transaction_test"
if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
    throw "NDK compiler not found: $Compiler"
}

$AdbArgs = @()
if ($Serial) { $AdbArgs = @("-s", $Serial) }

try {
    & $Compiler -std=c11 -O1 -g0 -static -Wall -Wextra -Werror `
        (Join-Path $Repo "tests\patcher_transaction_harness.c") -o $Output
    if ($LASTEXITCODE -ne 0) { throw "transaction harness compile failed" }

    & adb @AdbArgs push $Output /data/local/tmp/a2h_patcher_transaction_test | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "adb push failed" }
    & adb @AdbArgs shell chmod 0755 /data/local/tmp/a2h_patcher_transaction_test
    & adb @AdbArgs shell /data/local/tmp/a2h_patcher_transaction_test
    if ($LASTEXITCODE -ne 0) { throw "transaction harness failed" }
}
finally {
    & adb @AdbArgs shell rm -f /data/local/tmp/a2h_patcher_transaction_test 2>$null
    Remove-Item -LiteralPath $Output -Force -ErrorAction SilentlyContinue
}
