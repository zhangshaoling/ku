param(
    [uint64]$CallIterations = 1000000,
    [uint64]$LoadIterations = 10000,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$Benchmark = Join-Path $Root "kernel\out\cmake\bin\kernel_bench.exe"
$Activate = Join-Path $PSScriptRoot "activate_toolchain.ps1"

. $Activate

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build_kernel.ps1") -SkipTests
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

& $Benchmark $CallIterations $LoadIterations
exit $LASTEXITCODE
