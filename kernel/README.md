# Dao Kernel

This directory contains the new high-performance Dao runtime. It is independent of the legacy Python runtime, text frontend, stack VM, MCP server, and Agent/Memory modules.

## Layout

```text
include/dao/dao.h       Stable C embedding ABI
include/dao/format.hpp  Binary Module v1 and C++ builder API
src/module_builder.cpp  Deterministic binary encoder
src/runtime.cpp         Loader, verifier, and Register VM
tests/kernel_tests.cpp  Native conformance tests
fuzz/                   Sanitized module loader fuzz target and seed generator
FFI.md                  Numeric host import and callback contract
out/                    Local build output
```

## Build

```powershell
.\tools\build_kernel.ps1
```

The current ABI supports deterministic modules, numeric imports and exports, direct C host callbacks, zero-copy borrowed bytes and UTF-8 string views, `i64`, Trit, register arithmetic, three-way Trit branches, internal calls, and instruction budgets. Host calls do not use JSON, string opcode dispatch, or stored process addresses.

Run the initial load/call benchmark with:

```powershell
.\tools\benchmark_kernel.ps1
```

Run the module loader and verifier under libFuzzer, ASan, and UBSan with:

```powershell
.\tools\fuzz_kernel.ps1
```

The default fuzz run executes 100,000 inputs. Its corpus and crash artifacts remain under the ignored `kernel/out/fuzz/` directory.

Legacy code remains outside this directory only as migration input.
