# Kernel Migration Boundary

The new kernel is intentionally isolated from the legacy runtime.

## New Authority

- `kernel/include/dao/dao.h`: embedding ABI
- `kernel/include/dao/format.hpp`: Binary Module v1 and Register Bytecode ABI
- `kernel/src/runtime.cpp`: loader, verifier, and execution semantics
- `kernel/src/module_builder.cpp`: deterministic module encoder
- `kernel/tests/`: native conformance and ABI tests
- `kernel/benchmarks/`: performance baselines

## Legacy Inputs

The following remain available only for behavior extraction and later migration:

- Python runtime and compiler
- legacy stack VM
- text lexer/parser/compiler
- `.ku` standard library
- MCP, memory, graph, Tiandao, and life modules

No new kernel feature may depend on those implementations. Migration happens by adding conformance cases and a legacy-to-binary compiler, not by importing their runtime state into `kernel/`.

## First Migration Targets

1. Extract arithmetic, control-flow, call, UTF-8, and error cases from legacy parity tests.
2. Assign stable numeric opcodes and type contracts.
3. Implement the cases in Register Bytecode.
4. Add a migration compiler that emits Dao Binary Module v1.
5. Move applications only after the C ABI and module format are stable.
