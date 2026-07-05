param(
    [Parameter(Position = 0, ValueFromRemainingArguments = $true)]
    [string[]]$PytestArgs,
    [string]$Python = $env:PYTHON,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")

function Resolve-Python {
    param([string]$Requested)

    if ($Requested) {
        return $Requested
    }

    $cmd = Get-Command python -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    $cmd = Get-Command py -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    $venvPython = Join-Path $Root ".venv\Scripts\python.exe"
    if (Test-Path $venvPython) {
        return $venvPython
    }

    throw "No Python found. Pass -Python <path> or set the PYTHON environment variable."
}

$PythonExe = Resolve-Python -Requested $Python

if (-not $SkipBuild) {
    & (Join-Path $Root "tools\build_dao_core.ps1")
}

& $PythonExe -m json.tool (Join-Path $Root "syntaxes\ku.tmLanguage.json") | Out-Null

if (-not $PytestArgs -or $PytestArgs.Count -eq 0) {
    $PytestArgs = @("-q")
}

& $PythonExe -m pytest @PytestArgs

