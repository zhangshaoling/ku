# Ku Project Structure

This file is the working directory map for Ku. Product naming and document authority are defined in `KU_NAMING_AND_AUTHORITY.md`.

## Active Core

```text
kernel/
  include/dao/           Existing versioned C/C++ ABI and module-format identifiers
  src/                   Loader, verifier, Register VM, builder, migration compiler
  selfhost/compiler.ku   Ku self-hosted compiler
  stdlib/*.ku            Migrated Ku standard-library modules
  tests/                 Native conformance and integration tests
```

## Legacy Migration Core

`dao/` contains the legacy C VM, frontend, standard library, executable-memory and MCP prototypes. `ku/` contains earlier compatibility code. Both are migration inputs; new kernel behavior must not depend on their runtime state.

## Legacy Module ABI

The current C VM bootstrap loader implements a minimal source-level module ABI:

- Import syntax is `引 "std/name" 别 Alias`.
- Module specs are resolved under `dao/`; `std/math` maps to
  `dao/std/math.ku`.
- `.ku` is optional in import specs.
- `\` in specs is normalized to `/`.
- Absolute paths, drive-prefixed paths, `..` segments, empty specs, and `//`
  are rejected with `ImportError`.
- Module source bodies are loaded once per resolved path.
- The same module can be imported with multiple aliases.
- Alias wrappers are generated once per `(module path, alias)` pair.
- Public top-level thoughts are exported as `Alias_name(...)` wrappers.
- Thoughts beginning with `_` are internal and are not alias-exported.
- Thoughts already beginning with `Alias_` are not double-wrapped.

This ABI is source-concatenation based and belongs to the legacy C VM bridge. The new production module contract is defined by `kernel/FORMAT.md`; Ku module-path import migration is tracked in `KU_MIGRATION_PLAN.md` and `KU_SELFHOST.md`.

## Generated Fixtures

The following fixtures belong to the legacy migration path:

```text
demos/
  frontend_bootstrap.kub.json      Reusable C VM bootstrap image
  frontend_compile_demo.kub.json   Fixed frontend compilation demo
  semantic_std_combo.kub.json      Semantic std integration demo
  README.md                        Regeneration and run commands
```

Regenerate these through `tools/`; do not edit generated JSON by hand unless the
generator is also updated.

## Build And Generation Tools

The new kernel build and test entrypoint is:

```powershell
.\tools\build_kernel.ps1
```

Legacy C VM generation and verification tools remain available as migration support:

```text
tools/
  test.ps1
  verify_module.ps1
  verify_codex_mcp_tiandao.py
  generate_frontend_bootstrap.py
  generate_frontend_compile_demo.py
  generate_semantic_std_combo_demo.py
```

Python is still allowed here as build harness. The self-hosting goal is to shrink
this role over time, not to mix generator logic into tests or demos.

`tools/test.ps1` is the local Windows test entrypoint. `tools/verify_module.ps1`
is the smaller module smoke entrypoint for checking the C VM, frontend, std
imports, executable memory, and MCP gateway independently.

## Verification

```text
tests/
  test_c_vm_parity.py                  C VM behavior, bootstrap CLI, core self-check
  test_bootstrap_frontend_vm_execute.py
  test_bootstrap_compiler_vm_execute.py
  test_dao_parser_ku_bootstrap.py
  semantic_test_utils.py
```

Primary new-kernel check:

```powershell
.\tools\build_kernel.ps1
```

Legacy migration checks:

```bash
pytest tests/test_c_vm_parity.py -q
pytest -q
```

## Compatibility Layer

```text
ku/
  runtime.py
  compiler.py
  ku_lexer.py
  ku_parser.py
  std/
```

Treat `dao/` and `ku/` as migration, compatibility and historical harness code unless a task explicitly targets those paths.

## Documentation

```text
docs/
  KU_NAMING_AND_AUTHORITY.md   Product naming and document authority
  KU_SUBPROJECT_WORKSHEETS.md  Current subproject boundaries and acceptance
  KU_PROJECT_PROGRESS.md       Current evidence-based project status
  DAO_SYSTEM_ARCHITECTURE.md   Legacy Tiandao/memory/runtime migration map
  CODEX_MCP_TIANDAO.md         Local Codex MCP shared-memory setup
  C_VM_接管审计.md
  AGI母语语义内核规范.md
  MODULE_COMPLETION_PLAN.md
  PROJECT_STRUCTURE.md
  plans/
```

Architecture decisions and migration notes belong in `docs/`; executable demos
belong in `demos/`; generator scripts belong in `tools/`.

## Local Scratch And Backups

```text
backups/       Local non-git backups
scratch/       Disposable experiments
```

Do not add ad hoc root-level debug scripts for the self-hosting path. Put
repeatable generators in `tools/`, tests in `tests/`, and temporary probes under
`scratch/` or outside the repo.
