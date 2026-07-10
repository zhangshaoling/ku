# Dao Kernel Rewrite

Dao is being rebuilt as a high-performance machine-language runtime that any agent, model, application, or host can embed.

The new kernel does not design an Agent framework. It provides a deterministic binary module, a verified Register VM, Trit logic, and a stable C ABI.

```text
Host / Agent
  -> Dao C ABI
  -> verified Dao Binary Module
  -> Register VM
  -> optional AOT/JIT backend
```

## Current Kernel

The clean implementation lives in [`kernel/`](kernel/README.md):

- deterministic Dao Binary Module v1 builder
- strict section and instruction verifier
- numeric Register Bytecode ABI
- `i64` arithmetic and checked overflow
- balanced Trit `-1 / 0 / +1`
- explicit negative/zero/positive branches
- internal function calls and instruction budgets
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

- [Kernel implementation guide](docs/DAO_KERNEL_IMPLEMENTATION_GUIDE.md)
- [Binary Module and Bytecode v1](kernel/FORMAT.md)
- [Migration boundary](kernel/MIGRATION.md)
- [Benchmark baseline](kernel/BENCHMARKS.md)
- [Managed C++ toolchain](docs/TOOLCHAIN.md)

## Legacy Tree

The existing Python runtime, stack VM, text frontend, `.ku` standard library, MCP, memory, Tiandao, and life modules remain in this branch only as migration inputs.

They are not dependencies of the new `kernel/` implementation. New kernel behavior must be specified and tested inside `kernel/` before legacy behavior is migrated.
