# Dao Project Structure

This file is the working directory map for the self-hosting track. Keep the active
compiler/runtime path small and predictable; move or add files only when they fit
one of these buckets.

## Active Core

```text
dao/
  dao_core.c             C VM, builtins, bytecode executor, bootstrap source runner
  compiler.py            Python compiler harness used for parity and fixture builds
  dao_lexer.py           Python lexer harness
  dao_parser.py          Python parser harness
  runtime.py             Python runtime harness and legacy builtins
  test_core.ku           Dao core self-check program
  std/
    lexer.ku             Self-hosted lexer
    parser.ku            Self-hosted parser
    compiler.ku          Self-hosted bytecode compiler
    math.ku              Standard library module
    string.ku            Standard library module
    net.ku               Standard library module backed by C builtins
```

## Module ABI

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

This ABI is still source-concatenation based. It is the stable bridge before
moving to a real module object and bytecode-level import system.

## Generated Fixtures

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

```text
tools/
  test.ps1
  verify_module.ps1
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

Primary checks:

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

Treat `ku/` as compatibility and historical harness code unless a task is
explicitly about the packaged `ku` CLI.

## Documentation

```text
docs/
  DAO_SYSTEM_ARCHITECTURE.md   Canonical Dao/Tiandao/memory/runtime path map
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
