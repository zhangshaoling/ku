param(
    [string]$Output = "dao\dao_core.exe"
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$Source = Join-Path $Root "dao\dao_core.c"
$SqliteSource = Join-Path $Root "vendor\sqlite3.c"
$SqliteHeader = Join-Path $Root "vendor\sqlite3.h"
$OutPath = Join-Path $Root $Output
$OutDir = Split-Path -Parent $OutPath
if ($OutDir -and -not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir | Out-Null
}

$Inputs = @($Source, $SqliteSource, $SqliteHeader)
if ((Test-Path $OutPath) -and ($Inputs | ForEach-Object { Test-Path $_ } | Where-Object { -not $_ } | Measure-Object).Count -eq 0) {
    $OutTime = (Get-Item $OutPath).LastWriteTimeUtc
    $NewestInput = ($Inputs | ForEach-Object { (Get-Item $_).LastWriteTimeUtc } | Sort-Object -Descending | Select-Object -First 1)
    if ($OutTime -ge $NewestInput) {
        Write-Host "up to date $OutPath"
        exit 0
    }
}

$GccCandidates = @()
$gcc = Get-Command gcc -ErrorAction SilentlyContinue
if ($gcc) {
    $GccCandidates += $gcc.Source
}
$GccCandidates += @(
    "C:\msys64\ucrt64\bin\gcc.exe",
    "C:\msys64\mingw64\bin\gcc.exe",
    "C:\msys64\clang64\bin\gcc.exe",
    "C:\mingw64\bin\gcc.exe",
    "C:\MinGW\bin\gcc.exe",
    "C:\TDM-GCC-64\bin\gcc.exe"
)

function Invoke-GccBuild {
    param([string]$GccPath)

    $oldPath = $env:PATH
    try {
        if ($GccPath -like "C:\msys64\*") {
            $GccBin = Split-Path -Parent $GccPath
            $MsysUsrBin = "C:\msys64\usr\bin"
            $env:PATH = "$MsysUsrBin;$GccBin;$env:PATH"
        }

        & $GccPath -o $OutPath $Source $SqliteSource -lm -Wall -O2
        return $LASTEXITCODE
    } finally {
        $env:PATH = $oldPath
    }
}

foreach ($GccPath in $GccCandidates) {
    if (-not $GccPath -or -not (Test-Path $GccPath)) {
        continue
    }
    $rc = Invoke-GccBuild -GccPath $GccPath
    if ($rc -ne 0) {
        exit $rc
    }
    Write-Host "built $OutPath with gcc ($GccPath)"
    exit 0
}

$cl = Get-Command cl -ErrorAction SilentlyContinue
if ($cl) {
    & $cl.Source /nologo /O2 /Fe:$OutPath $Source $SqliteSource
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    Write-Host "built $OutPath with cl"
    exit 0
}

Write-Error "No native C compiler found. Install MinGW gcc or run from a Visual Studio Developer shell."
