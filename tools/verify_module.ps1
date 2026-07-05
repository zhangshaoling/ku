param(
    [Parameter(Position = 0)]
    [ValidateSet("all", "c-vm", "frontend", "std", "memory", "mcp")]
    [string]$Module = "all",
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

    throw "No Python found. Pass -Python <path> or set PYTHON."
}

function Invoke-Step {
    param(
        [string]$Name,
        [scriptblock]$Body
    )

    Write-Host "==> $Name"
    & $Body
}

function Invoke-DaoCore {
    param([string[]]$ArgsForDao)

    $exe = Join-Path $Root "dao\dao_core.exe"
    & $exe @ArgsForDao
    if ($LASTEXITCODE -ne 0) {
        throw "dao_core.exe failed: $($ArgsForDao -join ' ')"
    }
}

function Invoke-DaoCoreCapture {
    param([string[]]$ArgsForDao)

    $exe = Join-Path $Root "dao\dao_core.exe"
    $output = & $exe @ArgsForDao
    if ($LASTEXITCODE -ne 0) {
        throw "dao_core.exe failed: $($ArgsForDao -join ' ')"
    }
    return ($output -join "`n").Trim()
}

function Write-Utf8NoBom {
    param(
        [string]$Path,
        [string]$Text
    )

    $encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Text, $encoding)
}

$PythonExe = Resolve-Python -Requested $Python

if (-not $SkipBuild) {
    Invoke-Step "build C VM" {
        & (Join-Path $Root "tools\build_dao_core.ps1")
        if ($LASTEXITCODE -ne 0) {
            throw "C VM build failed"
        }
    }
}

function Test-CVm {
    Invoke-Step "C VM golden path" {
        Push-Location $Root
        try {
            Invoke-DaoCore -ArgsForDao @("--bootstrap", ".\demos\frontend_bootstrap.kub.json", ".\demos\golden_path.ku")
        } finally {
            Pop-Location
        }
    }

    Invoke-Step "C VM semantic std combo demo" {
        Push-Location $Root
        try {
            Invoke-DaoCore -ArgsForDao @(".\demos\semantic_std_combo.kub.json")
        } finally {
            Pop-Location
        }
    }
}

function Test-Frontend {
    Invoke-Step "frontend compile demo" {
        Push-Location $Root
        try {
            Invoke-DaoCore -ArgsForDao @(".\demos\frontend_compile_demo.kub.json")
        } finally {
            Pop-Location
        }
    }

    Invoke-Step "frontend source bootstrap" {
        Push-Location $Root
        try {
            Invoke-DaoCore -ArgsForDao @("--bootstrap", ".\demos\frontend_bootstrap.kub.json", ".\demos\golden_path.ku")
        } finally {
            Pop-Location
        }
    }
}

function Test-Std {
    Invoke-Step "std import aliases" {
        $writer = @'
from pathlib import Path

root = Path(__file__).resolve().parents[1]
yin = chr(0x5f15)
bie = chr(0x522b)
math_program = root / "scratch" / "verify_std_math.ku"
string_program = root / "scratch" / "verify_std_string.ku"
math_program.write_text(f'{yin} "std/math" {bie} M\nM_sum([1, 2, 3])\n', encoding="utf-8")
string_program.write_text(f'{yin} "std/string" {bie} S\nS_join(["dao", "vm"], "-")\n', encoding="utf-8")
print(math_program)
print(string_program)
'@
        $writerPath = Join-Path $Root "scratch\write_verify_std_module.py"
        Write-Utf8NoBom -Path $writerPath -Text $writer
        $programs = & $PythonExe $writerPath
        if ($LASTEXITCODE -ne 0) {
            throw "failed to write std verification program"
        }
        $mathProgram = $programs[0].Trim()
        $stringProgram = $programs[1].Trim()
        Push-Location $Root
        try {
            $mathOut = Invoke-DaoCoreCapture -ArgsForDao @("--bootstrap", ".\demos\frontend_bootstrap.kub.json", $mathProgram)
            if ($mathOut -ne "6") {
                throw "std math alias expected 6, got $mathOut"
            }
            $stringOut = Invoke-DaoCoreCapture -ArgsForDao @("--bootstrap", ".\demos\frontend_bootstrap.kub.json", $stringProgram)
            if ($stringOut -ne '"dao-vm"') {
                throw "std string alias expected `"dao-vm`", got $stringOut"
            }
            Write-Host "std ok"
        } finally {
            Pop-Location
        }
    }
}

function Test-Memory {
    Invoke-Step "C VM memory UTF-8 roundtrip" {
        $code = @'
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root))
from dao.c_vm_runtime import CVMRuntime

data_dir = root / "scratch" / "verify_module_memory_data"
data_dir.mkdir(parents=True, exist_ok=True)
rt = CVMRuntime(data_dir=data_dir)
topic = chr(0x5ba1) + chr(0x8ba1)

direct = rt.eval_code(
    f'db = sqlite_open(dao_data_path("utf8.db"))\n'
    f'sqlite_exec(db, "CREATE TABLE IF NOT EXISTS probe (x TEXT)", [])\n'
    f'sqlite_exec(db, "DELETE FROM probe", [])\n'
    f'sqlite_exec(db, "INSERT INTO probe VALUES (?)", ["{topic}"])\n'
    f'rows = sqlite_query(db, "SELECT x FROM probe", [])\n'
    f'sqlite_close(db)\n'
    f'rows[0]["x"]\n',
    profile="frontend",
)
assert direct.ok, direct.error or direct.stderr or direct.stdout
assert direct.value == topic, direct.value

recorded = rt.call_thought(
    "gap_record",
    [topic, "context", "missing", "next", "utf8"],
    params=["topic", "context", "missing", "next_action", "tags"],
    profile="memory",
)
assert recorded.ok, recorded.error or recorded.stderr or recorded.stdout

listed = rt.call_thought("gap_list_open", [20], params=["limit"], profile="memory")
assert listed.ok, listed.error or listed.stderr or listed.stdout
assert any(row.get("topic") == topic for row in listed.value.get("gaps", []))
print("memory ok")
'@
        $tmp = Join-Path $Root "scratch\verify_module_memory.py"
        Write-Utf8NoBom -Path $tmp -Text $code
        & $PythonExe $tmp
        if ($LASTEXITCODE -ne 0) {
            throw "memory verification failed"
        }
    }
}

function Test-Mcp {
    Invoke-Step "MCP golden path tool" {
        $code = @'
import json
import subprocess
import sys

proc = subprocess.Popen(
    [sys.executable, "-m", "dao.mcp_server"],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True,
    encoding="utf-8",
    cwd=".",
)

def send(payload):
    proc.stdin.write(json.dumps(payload, ensure_ascii=False) + "\n")
    proc.stdin.flush()
    return json.loads(proc.stdout.readline())

send({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
proc.stdin.write(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}) + "\n")
proc.stdin.flush()
response = send({
    "jsonrpc": "2.0",
    "id": 2,
    "method": "tools/call",
    "params": {"name": "ku_golden_path", "arguments": {}},
})
proc.terminate()
text = response["result"]["content"][0]["text"]
value = json.loads(text)["result"]
assert value["result"] == 42, value
print("mcp ok")
'@
        $tmp = Join-Path $Root "scratch\verify_module_mcp.py"
        Write-Utf8NoBom -Path $tmp -Text $code
        Push-Location $Root
        try {
            & $PythonExe $tmp
            if ($LASTEXITCODE -ne 0) {
                throw "MCP verification failed"
            }
        } finally {
            Pop-Location
        }
    }
}

switch ($Module) {
    "c-vm" { Test-CVm }
    "frontend" { Test-Frontend }
    "std" { Test-Std }
    "memory" { Test-Memory }
    "mcp" { Test-Mcp }
    "all" {
        Test-CVm
        Test-Frontend
        Test-Std
        Test-Memory
        Test-Mcp
    }
}

Write-Host "module verification passed: $Module"
