# Ku / Dao

**Ku / Dao** is an AI-native language runtime for executable memory:

<p align="center">
  <a href="https://github.com/zhangshaoling/ku/actions/workflows/ci.yml"><img src="https://github.com/zhangshaoling/ku/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <img src="https://img.shields.io/badge/version-0.9.0-blue" alt="Version 0.9.0">
  <img src="https://img.shields.io/badge/runtime-C%20VM-2ea44f" alt="C VM runtime">
  <img src="https://img.shields.io/badge/MCP-C%20VM%20default-6f42c1" alt="MCP C VM default">
  <img src="https://img.shields.io/badge/memory-SQLite%20experience-f97316" alt="SQLite experience memory">
  <img src="https://img.shields.io/badge/Python-harness%20only-64748b" alt="Python harness only">
  <img src="https://img.shields.io/badge/license-MIT-yellow" alt="MIT license">
</p>

```text
thought = code = memory
```

This repository is not trying to become a general-purpose scripting language.
Its purpose is narrower: make a thought writable as source, inspectable as
structured code, persistent as memory, executable by the C VM, and callable by
agents through MCP.

## Names

- **Ku** is the public package name, repository lineage, and historical language
  name.
- **Dao** is the active runtime line: Chinese-first syntax, self-hosted
  frontend, C VM execution, executable memory, and MCP tools.
- The repository may stay named `ku`, but new core runtime work should live
  under `dao/`.

## Current Status

The project has moved beyond the early Python prototype:

- `dao/dao_core.exe` runs committed bytecode demos and source through
  `--bootstrap`.
- `dao/std/lexer.ku`, `dao/std/parser.ku`, and `dao/std/compiler.ku` form the
  self-hosting frontend path.
- `dao/c_vm_runtime.py` is the Python gateway for invoking the C VM from tests
  and MCP.
- SQLite-backed experience memory, task queues, gaps, datasets, and data memory
  records persist under `DAO_DATA_DIR`.
- MCP tools use the C VM by default; Python semantic fallback is opt-in only for
  parity/debug work.

Python is still allowed as packaging, test harness, fixture generation, and MCP
stdio glue. It must not silently become the semantic authority.

## Architecture

```text
Dao source
  -> self-hosted lexer/parser/compiler
  -> bytecode
  -> C VM
  -> executable memory
  -> MCP tool surface
```

The practical bootstrap path still uses generated fixtures:

```text
Python harness -> bootstrap image -> C VM -> Dao frontend -> Dao source
```

The long-term direction is to move more of that generation and runtime support
into Dao itself.

## Repository Map

```text
dao/        Active Dao runtime, C VM, MCP server, and std .ku modules
demos/      Generated bootstrap images and checked demo bytecode
docs/       Architecture notes, module gates, and migration records
ku/         Legacy compatibility package and historical Python runtime
syntaxes/   Ku syntax highlighting assets
tests/      Python, frontend, C VM, memory, and MCP regression tests
tools/      Build, verification, and fixture generation scripts
vendor/     Vendored runtime dependencies such as SQLite amalgamation
```

Maintained maps:

- `docs/PROJECT_STRUCTURE.md`
- `docs/MODULE_COMPLETION_PLAN.md`
- `docs/PROJECT_CONSTITUTION.md`

## Quick Verification

On Windows, run the local test gate:

```powershell
.\tools\test.ps1 -q
```

Run module smoke checks:

```powershell
.\tools\verify_module.ps1 c-vm
.\tools\verify_module.ps1 frontend
.\tools\verify_module.ps1 std
.\tools\verify_module.ps1 memory
.\tools\verify_module.ps1 mcp
```

Build the C VM:

```powershell
.\tools\build_dao_core.ps1
```

## MCP

Run the MCP server:

```powershell
python -m dao.mcp_server
```

The default `ku_eval`, `ku_call`, `ku_golden_path`, and experience-memory tools
run through the C VM gateway. If the C VM binary is missing, `ku_eval` fails
loudly by default.

Python fallback for `ku_eval` is reserved for debug/parity work:

```powershell
$env:DAO_MCP_ALLOW_PYTHON_FALLBACK = "1"
```

## Philosophy

Dao treats memory as something that can run, and code as something that can be
remembered, inspected, linked, and rewritten.

The finish line is not "a nicer syntax." The finish line is an executable memory
system where an agent can load a thought, inspect it, run it, persist its result,
and evolve the system without Python owning the meaning.

MIT licensed.
