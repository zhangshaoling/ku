$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$Sdk = Join-Path $Root "kernel\out\sdk"
$Build = Join-Path $Root "kernel\out\sdk-consumer"
. (Join-Path $PSScriptRoot "activate_toolchain.ps1")
& cmake -S (Join-Path $Root "kernel\tests\sdk_consumer") -B $Build -G Ninja "-DCMAKE_PREFIX_PATH=$Sdk"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --build $Build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$env:PATH = "$(Join-Path $Sdk 'bin');$env:PATH"
& (Join-Path $Build "bin\dao_sdk_consumer.exe")
exit $LASTEXITCODE
