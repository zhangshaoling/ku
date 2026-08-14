# Ku Kernel (`dao_*` ABI)

This directory contains the new high-performance Ku runtime. Existing `dao_*` ABI and Dao Binary Module names are retained as versioned implementation identifiers. The kernel is independent of the legacy Python runtime, text frontend, stack VM, MCP server, and Agent/Memory modules.

## Layout

```text
include/dao/dao.h       Stable C embedding ABI
include/dao/format.hpp  Binary Module v1 and C++ builder API
src/module_builder.cpp  Deterministic binary encoder
src/runtime.cpp         Loader, verifier, and Register VM
tests/kernel_tests.cpp  Native conformance tests
fuzz/                   Sanitized module loader fuzz target and seed generator
FFI.md                  Numeric host import and callback contract
OWNERSHIP.md            Frozen C ABI value and container lifetime contract
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

K5 currently provides a verified-module cache, direct predecoded instruction pointers,
a numeric portable-C AOT backend with branch/loop/internal-call support, installable CMake SDK, and Python/Rust bindings. See
[`K5.md`](K5.md). Build the release ZIP with:

```powershell
.\tools\package_kernel.ps1
```

Compile the supported scalar Ku subset to a verified Dao module with:

```powershell
.\kernel\out\cmake\bin\dao-ku.exe input.ku output.dao
```

`dao-ku` accepts legacy source imports such as `引 "std/math" 别 math` and
`import "std/math" as math`. Imported functions are namespaced as `math_<name>`;
resolution is recursive, cycle-checked, and restricted to the input module root.

The frontend self-hosting path has started: `kernel/selfhost/compiler.ku` implements the
SH0 seed and the SH1a expression parser, generating a verified executable module through
the typed-builder Host ABI. See [`../docs/KU_SELFHOST.md`](../docs/KU_SELFHOST.md).

The migration frontend supports functions (`thought`, `func`, or `思`), integer, Trit,
and string literals, lists, string-keyed maps, indexing, variables, assignment,
arithmetic, comparisons, internal calls, explicit host imports, `if/else`, `while`,
`for..in`, `break`, `continue`, `try/catch/throw`, and `return`.
Host dependencies use `import name(arity)` and are resolved through the numeric C ABI.
The authoritative source-language decisions and compatibility levels are defined in
[`../docs/KU_V1_SEMANTICS.md`](../docs/KU_V1_SEMANTICS.md).
Lists and maps are owned by the VM's top-level call arena. A returned container remains
valid until the next call on that VM; VM-owned containers cannot be passed back as
arguments to a later top-level call. See [`OWNERSHIP.md`](OWNERSHIP.md) for the frozen
C ABI value-lifetime and Host-container contract.

The packaged migrated standard-library subset includes `stdlib/core.ku`, the
integer/list-compatible `stdlib/math.ku`, and the list-read subset in `stdlib/list.ku`.

Run the module loader and verifier under libFuzzer, ASan, and UBSan with:

```powershell
.\tools\fuzz_kernel.ps1
```

The default fuzz run executes 100,000 inputs. Its corpus and crash artifacts remain under the ignored `kernel/out/fuzz/` directory.

Legacy code remains outside this directory only as migration input.
