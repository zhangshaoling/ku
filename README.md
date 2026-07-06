# Ku / Dao

**Ku / Dao** is an AI-native language runtime for executable memory:

<p align="center">
  <a href="https://github.com/zhangshaoling/ku/actions/workflows/ci.yml"><img src="https://github.com/zhangshaoling/ku/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <img src="https://img.shields.io/badge/version-0.9.0-blue" alt="Version 0.9.0">
  <img src="https://img.shields.io/badge/runtime-C%20VM-2ea44f" alt="C VM runtime">
  <img src="https://img.shields.io/badge/MCP-C%20VM%20default-6f42c1" alt="MCP C VM default">
  <img src="https://img.shields.io/badge/memory-SQLite%20six--table-f97316" alt="SQLite six-table memory">
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

The canonical system map is `docs/DAO_SYSTEM_ARCHITECTURE.md`. The long-term
AGI memory direction is defined in `docs/AGI_EXECUTABLE_MEMORY_ROADMAP.md`.

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
- `dao/std/memory.ku` defines the six-table Dao memory model:
  Experience, Knowledge, Tool, Concept, Goal, and Relation.
- `dao/std/tiandao_mcp.ku` provides the low-latency Tiandao MCP hot path for
  agent scheduling and memory recording.
- Selected memory records can be promoted into callable thought/tool candidates
  through the C VM-backed MCP path, and active promotions appear as dynamic
  `ku_memory_<thought_name>` tools.
- Memory recall can return explainability metadata, and promotion suggestions
  use local Dao policy rules without calling an external model.
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

Run a `.ku` file through the C VM gateway:

```powershell
uv run python -m dao run demos/golden_path.ku --profile frontend
uv run python -m dao run demos/memory_6table_golden_path.ku --profile frontend --module dao/std/memory.ku
```

Start the minimal C VM-backed REPL:

```powershell
uv run python -m dao repl --profile frontend
```

Build the C VM:

```powershell
.\tools\build_dao_core.ps1
```

The checked-in golden path demo is `demos/golden_path.ku`. It exercises the
current source -> frontend -> bytecode -> C VM path and is covered by:

```powershell
.\tools\verify_module.ps1 c-vm
.\tools\verify_module.ps1 mcp
```

## MCP

Run the MCP server:

```powershell
python -m dao.mcp_server
```

A local MCP client can use this server definition:

```json
{
  "mcpServers": {
    "dao_tiandao": {
      "command": "A:\\path\\to\\ku\\.venv\\Scripts\\python.exe",
      "args": ["-m", "dao.mcp_server"],
      "env": {
        "PYTHONPATH": "A:\\path\\to\\ku",
        "DAO_DATA_DIR": "A:\\path\\to\\ku\\.dao_data"
      }
    }
  }
}
```

The default `ku_eval`, `ku_call`, `ku_golden_path`, `ku_tiandao`,
`ku_tiandao_stats`, and experience-memory tools run through the C VM gateway.
Memory recall and promotion are exposed through `ku_recall_memory`,
`ku_recall_memory_explain`, `ku_promote_memory`, `ku_list_memory_promotions`,
`ku_suggest_memory_promotions`, and `ku_call_memory`. Active promotions are also
exposed as dynamic `ku_memory_<thought_name>` tools. If the C VM binary is
missing, `ku_eval` fails loudly by default.

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
