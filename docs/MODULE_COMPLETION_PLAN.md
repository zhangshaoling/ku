# Dao Module Completion Plan

This file is the module-by-module completion map for Dao/Ku. It keeps the
original goal visible while the project grows:

```text
thought = code = memory
```

Dao is not being built as a generic scripting language. The project is complete
only when a thought can be written as Dao source, inspected as structured code,
persisted as memory, executed by the C VM, exposed to agents through MCP, and
verified without Python being the semantic authority.

The long-term AGI memory direction is defined in
`docs/AGI_EXECUTABLE_MEMORY_ROADMAP.md`.

## Completion Rule

Every module below must name:

- the files that own it
- what works now
- what remains
- the command that proves the claim

Python may remain as packaging, test harness, fixture generation, and MCP stdio
glue. Python must not silently become the default semantic execution path.

## Module 1: C VM Kernel

Owner files:

- `dao/dao_core.c`
- `dao/c_vm_runtime.py`
- `tools/build_dao_core.ps1`
- `tests/test_c_vm_parity.py`
- `tests/test_c_vm_runtime_gateway.py`

Current state:

- Native Windows `dao_core.exe` exists.
- The C VM runs committed bytecode demos.
- The C VM runs source through `--bootstrap`.
- SQLite, `dao_data_path`, `now`, and `now_fmt` are available.
- UTF-8 SQLite roundtrips are covered through the runtime gateway test.

Remaining:

- Long-running memory ownership audit.
- Reduce frame-level hard exits and stderr-only startup errors where needed for
  daemon use.
- Keep expanding parity only when behavior changes.

Proof:

```powershell
.\tools\verify_module.ps1 c-vm
.\tools\test.ps1 tests/test_c_vm_runtime_gateway.py -q
```

## Module 2: Self-Hosted Frontend

Owner files:

- `dao/std/lexer.ku`
- `dao/std/parser.ku`
- `dao/std/compiler.ku`
- `demos/frontend_bootstrap.kub.json`
- `demos/frontend_compile_demo.kub.json`
- `tools/generate_frontend_bootstrap.py`
- `tools/generate_frontend_compile_demo.py`

Current state:

- The C VM can run the frontend bootstrap image.
- The frontend can compile and execute a checked-in source demo.
- Regeneration tools exist.

Remaining:

- Move more fixture generation out of Python when the Dao compiler path is
  strong enough.
- Replace source-concatenation imports with a real module object or bytecode
  import system.

Proof:

```powershell
.\tools\verify_module.ps1 frontend
```

## Module 3: Standard Library And Module ABI

Owner files:

- `dao/std/*.ku`
- `dao/dao_core.c` import loader
- `docs/PROJECT_STRUCTURE.md`
- `tests/test_c_vm_parity.py`

Current state:

- Core math, string, list, type, semantic, patch, trace, memory, task queue, and
  experience modules can be loaded through C VM profiles.
- Source imports use `引 "std/name" 别 Alias`.
- Import path safety rules are documented.

Remaining:

- Split stable public std APIs from bootstrap-only helpers.
- Add module-level docs for each stable std module.
- Replace alias wrapper generation when bytecode-level modules land.

Proof:

```powershell
.\tools\verify_module.ps1 std
```

## Module 4: Executable Memory

Owner files:

- `dao/std/experience.ku`
- `dao/std/task_queue.ku`
- `dao/std/memory_env.ku`
- `dao/std/semantic_core.ku`
- `dao/std/trace.ku`
- `dao/std/patch.ku`
- `tests/test_experience_memory.py`
- `tests/test_c_vm_runtime_gateway.py`

Current state:

- Experience and task queue data persist under `DAO_DATA_DIR`.
- Gaps, datasets, data memories, and gap-to-task links run through C VM-backed
  MCP tools.
- UTF-8 topic storage is covered.

Remaining:

- Define retention, compaction, and migration rules for long-lived experience
  databases.
- Make memory records directly callable as thought/tool surfaces, not just
  stored rows.

Proof:

```powershell
.\tools\verify_module.ps1 memory
```

## Module 5: MCP Agent Gateway

Owner files:

- `dao/mcp_server.py`
- `dao/c_vm_runtime.py`
- `tests/test_dao_mcp_server.py`
- `tests/test_experience_memory.py`

Current state:

- `ku_eval`, `ku_call`, `ku_golden_path`, and experience tools use the C VM
  gateway by default.
- Python is still used for stdio framing and schema discovery.
- Python fallback for `ku_eval` is opt-in only through
  `DAO_MCP_ALLOW_PYTHON_FALLBACK=1`; missing C VM binaries fail loudly by
  default.
- Default `ku_eval` and `ku_call` paths are guarded by a regression test that
  fails if Python `Thought.call` becomes the semantic execution path.

Remaining:

- Keep JSON-RPC stdout clean and runtime logs on stderr.

Proof:

```powershell
.\tools\verify_module.ps1 mcp
```

## Module 6: Tooling, Release, And Ecosystem

Owner files:

- `tools/test.ps1`
- `tools/verify_module.ps1`
- `.github/workflows/ci.yml`
- `syntaxes/ku.tmLanguage.json`
- `docs/linguist/*`
- sibling fork `../D_Tools_Linguist`

Current state:

- Windows CI installs Python, pytest, and MSYS2 GCC.
- Local test and module verification entry points exist.
- Ku Linguist staging exists in the sibling fork.

Remaining:

- Keep root-level diagnostics out of the committed language runtime.
- Publish a dedicated Ku grammar package before opening an upstream Linguist PR.
- Align README/release notes with real C VM responsibility at each release.

Proof:

```powershell
.\tools\test.ps1 -q
```

## Near-Term Finish Order

1. Lock current C VM/MCP/memory behavior with tests.
2. Remove or gate Python semantic fallback.
3. Audit C VM long-process memory ownership.
4. Stabilize std module public APIs.
5. Move bootstrap generation out of Python when Dao can own the path.
6. Cut a release only after README, demos, tests, and MCP claims all match.
