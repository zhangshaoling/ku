param(
    [string]$ToolchainRoot
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if (-not $ToolchainRoot) {
    $ToolchainRoot = Join-Path $RepoRoot ".toolchain"
}
$ToolchainRoot = [System.IO.Path]::GetFullPath($ToolchainRoot)
$ManifestPath = Join-Path $ToolchainRoot "manifest.json"
$LlvmBin = Join-Path $ToolchainRoot "llvm-mingw\bin"
$CMakeBin = Join-Path $ToolchainRoot "cmake\bin"
$NinjaBin = Join-Path $ToolchainRoot "ninja"

if (-not (Test-Path -LiteralPath $ManifestPath)) {
    throw "Toolchain manifest is missing. Run .\tools\bootstrap_toolchain.ps1 first."
}

$manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
foreach ($package in $manifest.packages) {
    if (-not (Test-Path -LiteralPath $package.path)) {
        throw "Toolchain package is missing: $($package.name)"
    }

    if ($package.PSObject.Properties.Name -contains "archive") {
        $archivePath = Join-Path $ToolchainRoot "cache\$($package.archive)"
        if (-not (Test-Path -LiteralPath $archivePath)) {
            throw "Cached toolchain archive is missing: $($package.archive)"
        }
        $actual = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -ne $package.sha256) {
            throw "SHA-256 mismatch for cached toolchain archive: $($package.archive)"
        }
    }
}

$tools = [ordered]@{
    "C compiler" = (Join-Path $LlvmBin "clang.exe")
    "C++ compiler" = (Join-Path $LlvmBin "clang++.exe")
    "linker" = (Join-Path $LlvmBin "ld.lld.exe")
    "build configuration" = (Join-Path $CMakeBin "cmake.exe")
    "build executor" = (Join-Path $NinjaBin "ninja.exe")
    "debugger" = (Join-Path $LlvmBin "lldb.exe")
    "debug adapter" = (Join-Path $LlvmBin "lldb-dap.exe")
    "language server" = (Join-Path $LlvmBin "clangd.exe")
    "formatter" = (Join-Path $LlvmBin "clang-format.exe")
    "static analysis" = (Join-Path $LlvmBin "clang-tidy.exe")
    "dependency scanner" = (Join-Path $LlvmBin "clang-scan-deps.exe")
    "coverage report" = (Join-Path $LlvmBin "llvm-cov.exe")
    "profile merge" = (Join-Path $LlvmBin "llvm-profdata.exe")
    "object inspection" = (Join-Path $LlvmBin "llvm-objdump.exe")
    "binary metadata" = (Join-Path $LlvmBin "llvm-readobj.exe")
    "symbol inspection" = (Join-Path $LlvmBin "llvm-nm.exe")
    "symbolizer" = (Join-Path $LlvmBin "llvm-symbolizer.exe")
    "resource compiler" = (Join-Path $LlvmBin "llvm-rc.exe")
}

foreach ($entry in $tools.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $entry.Value)) {
        throw "Required $($entry.Key) is missing: $($entry.Value)"
    }
}

$clangRuntime = Get-ChildItem (Join-Path $ToolchainRoot "llvm-mingw\lib\clang") -Directory |
    Sort-Object Name -Descending |
    Select-Object -First 1
if (-not $clangRuntime) {
    throw "Clang runtime directory is missing."
}
$runtimeRoot = Join-Path $clangRuntime.FullName "lib\windows"
$runtimes = [ordered]@{
    "AddressSanitizer" = (Join-Path $LlvmBin "libclang_rt.asan_dynamic-x86_64.dll")
    "UndefinedBehaviorSanitizer" = (Join-Path $runtimeRoot "libclang_rt.ubsan_standalone-x86_64.a")
    "libFuzzer" = (Join-Path $runtimeRoot "libclang_rt.fuzzer-x86_64.a")
    "coverage profiling" = (Join-Path $runtimeRoot "libclang_rt.profile-x86_64.a")
}

foreach ($entry in $runtimes.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $entry.Value)) {
        throw "Required $($entry.Key) runtime is missing: $($entry.Value)"
    }
}

$clangVersion = (& (Join-Path $LlvmBin "clang++.exe") --version | Select-Object -First 1)
$cmakeVersion = (& (Join-Path $CMakeBin "cmake.exe") --version | Select-Object -First 1)
$ninjaVersion = (& (Join-Path $NinjaBin "ninja.exe") --version | Select-Object -First 1)

Write-Output "toolchain healthy: $ToolchainRoot"
Write-Output $clangVersion
Write-Output $cmakeVersion
Write-Output "ninja version $ninjaVersion"
Write-Output "$($tools.Count) commands and $($runtimes.Count) runtime capabilities verified"
