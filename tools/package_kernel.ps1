param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$Build = Join-Path $Root "kernel\out\cmake"
. (Join-Path $PSScriptRoot "activate_toolchain.ps1")

& (Join-Path $PSScriptRoot "build_kernel.ps1") -Configuration $Configuration -SkipTests
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& ctest --test-dir $Build --output-on-failure -E aot_performance_gate
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --install $Build --prefix (Join-Path $Root "kernel\out\sdk")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cpack --config (Join-Path $Build "CPackConfig.cmake") -B (Join-Path $Root "kernel\out\packages")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$Version = (Get-Content (Join-Path $Root "CMakeLists.txt") | Select-String 'project\(dao_kernel VERSION ([0-9.]+)').Matches.Groups[1].Value
$Package = Get-ChildItem (Join-Path $Root "kernel\out\packages\Ku-$Version-Windows-*.zip") | Select-Object -First 1
& python (Join-Path $PSScriptRoot "verify_installed_sdk.py") $Package.FullName
exit $LASTEXITCODE
