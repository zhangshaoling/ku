# Ku Kernel (`dao_*` ABI)

This directory contains the new high-performance Ku runtime. Existing `dao_*` ABI and Dao Binary Module names are retained as versioned implementation identifiers. The kernel is independent of the legacy Python runtime, text frontend, stack VM, MCP server, and Agent/Memory modules.

## Layout

```text
include/dao/dao.h       Stable C embedding ABI
include/dao/format.hpp  Binary Module v2/ABI10 and C++ builder API
include/dao/module_store.hpp  Identified module persistence API
src/module_builder.cpp  Deterministic binary encoder
src/runtime.cpp         Loader, verifier, and Register VM
src/selfhost.cpp        Host bridge for the self-hosted compiler
src/module_store.cpp    Immutable code-memory records and writer lock
tools/ku_compile.cpp    Compile, check, format, and store CLI
tests/kernel_tests.cpp  Native conformance tests
fuzz/                   Sanitized module loader fuzz target and seed generator
FFI.md                  Numeric host import and callback contract
OWNERSHIP.md            Frozen C ABI value and container lifetime contract
out/                    Local build output
```

## Build

```powershell
.\tools\bootstrap_toolchain.ps1
$env:CMAKE_BUILD_PARALLEL_LEVEL = '2'
.\tools\build_kernel.ps1 -SkipTests
. .\tools\activate_toolchain.ps1
ctest --test-dir kernel/out/cmake --output-on-failure -E aot_performance_gate
```

The bootstrap command installs the managed Windows toolchain on first use. Linux/macOS
build prerequisites and commands are in the [root quick start](../README.md#快速开始).
The functional command above follows CI by excluding the independent AOT performance gate.

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

Compile, check, or format supported Ku source with the default self-hosted frontend.
The formatter writes to stdout.

```powershell
.\kernel\out\cmake\bin\dao-ku.exe input.ku output.dao
.\kernel\out\cmake\bin\dao-ku.exe --check input.ku
.\kernel\out\cmake\bin\dao-ku.exe --fmt input.ku
```

The native code-memory path does not require Python:

```powershell
.\kernel\out\cmake\bin\dao-ku.exe --store .ku_agent_memory --identity ku:agent/example 1.0.0 input.ku
.\kernel\out\cmake\bin\dao-module verify .ku_agent_memory ku:agent/example 1.0.0
.\kernel\out\cmake\bin\dao-module run .ku_agent_memory ku:agent/example 1.0.0 main
```

`dao-ku --store` is the production code-memory entry point. Python bindings are
compatibility tooling and tests only; they are not required to save, verify, or execute
code memories.

Code-memory records preserve module bytes, exact identity/version, optional source, and
parent provenance. Saves use a nonblocking OS writer lock, with the manifest published
last and same-input retries for interrupted writes. Schema, compatibility, and recovery
guarantees are specified in [MODULE_ABI.md](MODULE_ABI.md#store-manifest-schema-1).

`dao-ku` accepts legacy source imports such as `引 "std/math" 别 math` and
`import "std/math" as math`. Imported functions are namespaced as `math_<name>`;
resolution is recursive, cycle-checked, and restricted to the input module root.

`kernel/selfhost/compiler.ku` is the default frontend. The C++ recovery compiler bootstraps
it, and typed-builder Host capabilities encode the resulting module. Self-rebuild tests
check `B0 == B1 == B2` and recovery/selfhost byte identity for the acceptance corpus.
`--recovery` explicitly selects the C++ frontend. See [KU_SELFHOST.md](../docs/KU_SELFHOST.md).

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

The packaged standard-library modules are `core`, `math`, `list`, `type`, `fs`, `io`,
`debug`, `string`, `error`, `http`, and `lexer`. Exact identities, dependencies, exported
arities, and Host requirements are defined in [STDLIB_ABI.md](STDLIB_ABI.md).
`lexer` currently uses `--recovery` for its legacy multiline-list syntax. File and HTTP
operations require the corresponding Host capabilities to be registered by the embedder.

Run the module loader and verifier under libFuzzer, ASan, and UBSan with:

```powershell
.\tools\fuzz_kernel.ps1
```

The default fuzz run executes 100,000 inputs. Its corpus and crash artifacts remain under the ignored `kernel/out/fuzz/` directory.

Legacy code remains outside this directory only as migration input.
