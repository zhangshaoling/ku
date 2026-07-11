param([Parameter(Mandatory=$true)][string]$InputModule,
      [Parameter(Mandatory=$true)][string]$OutputLibrary)
$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
. (Join-Path $PSScriptRoot "activate_toolchain.ps1")
$Aot = Join-Path $Root "kernel\out\cmake\bin\dao-aot.exe"
if (-not (Test-Path -LiteralPath $Aot)) { & (Join-Path $PSScriptRoot "build_kernel.ps1") -SkipTests }
$Source = [System.IO.Path]::ChangeExtension([System.IO.Path]::GetFullPath($OutputLibrary), ".c")
& $Aot ([System.IO.Path]::GetFullPath($InputModule)) $Source
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& clang -std=c17 -O3 -shared $Source -o ([System.IO.Path]::GetFullPath($OutputLibrary))
exit $LASTEXITCODE
