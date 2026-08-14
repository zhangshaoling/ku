param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$Build = Join-Path $Root "kernel\out\cmake"
. (Join-Path $PSScriptRoot "activate_toolchain.ps1")

& (Join-Path $PSScriptRoot "build_kernel.ps1") -Configuration $Configuration
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --install $Build --prefix (Join-Path $Root "kernel\out\sdk")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cpack --config (Join-Path $Build "CPackConfig.cmake") -B (Join-Path $Root "kernel\out\packages")
exit $LASTEXITCODE
