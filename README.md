# Ku Language System

Ku is an AI machine-language system being rebuilt on a high-performance runtime that any agent, model, application, or host can embed.

Existing `dao_*` symbols, Dao Binary Module names, and the `dao/` directory are implementation and compatibility identifiers. They do not represent a second language. See the [Ku naming and authority decision](docs/KU_NAMING_AND_AUTHORITY.md).

The new kernel does not design an Agent framework. It provides a deterministic binary module, a verified Register VM, Trit logic, and a stable C ABI.

```text
Host / Agent
  -> Ku C ABI (current dao_* symbols)
  -> verified Dao Binary Module
  -> Register VM
  -> optional AOT/JIT backend
```

## Canonical Syntax

Ku's canonical surface syntax is Chinese. The idiomatic form is:

```ku
思 加一(x) { x + 1 }
```

English keywords (`thought`, `if`, `return`, `import`, …) compile to identical bytecode but are
compatibility-tier: they are not shown in examples or entry points and may be removed after a
migration window. See [Ku v1 semantics](docs/KU_V1_SEMANTICS.md).

## Current Kernel

The clean implementation lives in [`kernel/`](kernel/README.md):

- deterministic Dao Binary Module builder (current format: v2 / ABI10; v1 / ABI9 also accepted per kernel/FORMAT.md)
- strict section and instruction verifier
- numeric Register Bytecode ABI
- `i64` arithmetic and checked overflow
- balanced Trit `-1 / 0 / +1`
- explicit negative/zero/positive branches
- internal function calls and instruction budgets
- zero-copy borrowed bytes and UTF-8 string views
- dynamically linked C ABI and pure C header smoke test
- native conformance and performance benchmarks

Build and test on Windows:

```powershell
.\tools\build_kernel.ps1
```

Run the baseline benchmark:

```powershell
.\tools\benchmark_kernel.ps1 -SkipBuild
```

## Authority

- [Ku naming and document authority](docs/KU_NAMING_AND_AUTHORITY.md)
- [Ku v1 language semantics](docs/KU_V1_SEMANTICS.md)
- [Ku subproject worksheets](docs/KU_SUBPROJECT_WORKSHEETS.md)
- [Ku current project progress](docs/KU_PROJECT_PROGRESS.md)
- [Kernel implementation guide](docs/DAO_KERNEL_IMPLEMENTATION_GUIDE.md)
- [Binary Module and Bytecode v1](kernel/FORMAT.md)
- [Migration boundary](kernel/MIGRATION.md)
- [Benchmark baseline](kernel/BENCHMARKS.md)
- [Managed C++ toolchain](docs/TOOLCHAIN.md)

## Legacy Tree

The existing Python runtime, legacy C VM, text frontend, `.ku` standard library, MCP, memory, Tiandao, and life modules under `dao/` and `ku/` remain in this branch as migration inputs.

They are not dependencies of the new `kernel/` implementation. New kernel behavior must be specified and tested inside `kernel/` before legacy behavior is migrated.
