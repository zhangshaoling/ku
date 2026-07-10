$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$ToolchainRoot = Join-Path $RepoRoot ".toolchain"
$LlvmBin = Join-Path $ToolchainRoot "llvm-mingw\bin"
$CMakeBin = Join-Path $ToolchainRoot "cmake\bin"
$NinjaBin = Join-Path $ToolchainRoot "ninja"

$required = @(
    (Join-Path $LlvmBin "clang++.exe"),
    (Join-Path $CMakeBin "cmake.exe"),
    (Join-Path $NinjaBin "ninja.exe")
)
$missing = @($required | Where-Object { -not (Test-Path -LiteralPath $_) })
if ($missing.Count -ne 0) {
    throw "Dao toolchain is incomplete. Run .\tools\bootstrap_toolchain.ps1 first."
}

$env:PATH = "$LlvmBin;$CMakeBin;$NinjaBin;$env:PATH"
$env:CC = Join-Path $LlvmBin "clang.exe"
$env:CXX = Join-Path $LlvmBin "clang++.exe"
$env:CMAKE_GENERATOR = "Ninja"

Write-Output "Dao toolchain activated: $ToolchainRoot"
