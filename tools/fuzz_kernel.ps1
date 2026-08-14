param(
    [uint64]$Runs = 100000,
    [uint32]$MaxInputBytes = 1048576
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$Build = Join-Path $Root "kernel\out\fuzz"
$Corpus = Join-Path $Build "corpus"
$Artifacts = Join-Path $Build "artifacts"
$Activate = Join-Path $PSScriptRoot "activate_toolchain.ps1"

. $Activate

& cmake -S $Root -B $Build -G Ninja `
    -DCMAKE_BUILD_TYPE=RelWithDebInfo `
    -DDAO_ENABLE_SANITIZERS=ON `
    -DDAO_BUILD_FUZZER=ON
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& cmake --build $Build --target dao_module_fuzz dao_fuzz_seed
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

New-Item -ItemType Directory -Force -Path $Corpus, $Artifacts | Out-Null
$Seed = Join-Path $Corpus "minimal.dao"
& (Join-Path $Build "bin\dao_fuzz_seed.exe") $Seed
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& (Join-Path $Build "bin\dao_module_fuzz.exe") $Corpus `
    "-runs=$Runs" `
    "-max_len=$MaxInputBytes" `
    "-artifact_prefix=$Artifacts\"
exit $LASTEXITCODE
