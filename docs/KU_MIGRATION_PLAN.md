# K4 legacy `.ku` migration compiler — staged plan

## Background
- Ku = self-hosted language (`ku/`): lexer, parser, runtime, stdlib, macro (`thought`), import system.
- Old compiler (`ku/compiler.py`) emits a Python-executable bytecode for the stack VM. Per guide §15 this is migration **input**, not something to extend.
- New target: Dao Binary Module v1 + `ModuleBuilder` typed builder + the versioned
  register opcode ABI (v5 after K4 language migration extensions).
- Goal: route `*.ku` -> typed builder -> `encode()` -> verifiable Dao Binary Module.

## Staged delivery

### Phase E — emission harness (complete)
- Introduce `ku_migration` library and `km::Compiler`.
- Front does not change; just emit typed-builder calls for a single fixed `.ku` smoke program, run `km verify`, hand bytes to the existing loader+verifier.
- Establishes the code path end-to-end.

### Phase L — expression MVP (complete)
- Lexer + Pratt parser -> `ModuleBuilder`.
- Implemented: integer/null/bool literals, assignment, `and/or/not`, unary minus,
  `+ - * / %`, integer comparisons, and forward/internal function calls within a unit.
- Implemented by VM ABI v6: module-owned UTF-8 string constants, lists, string-keyed maps,
  and dynamic list/map indexing.
- Per-function header: registers/parameters/instructions derived from actual code.
- Verification: every output passes `loader+verifier` and round-trips back through the assembler (existing tools).

### Phase C — control-flow MVP (complete)
- Implemented: nested `if/else`, `while`, `break/continue`, and `return`, with strict
  Trit conditions and verified branch-target backpatching.
- Implemented: list `for..in` and cross-function `try/catch/throw` propagation.
- Trit-aware branch opcodes (BR_TRIT_NEG/ZERO/POS) for Ku trit semantics.
- Function-call + self-recursion; nested functions lowered to flat calls.

### Phase I — imports and FFI (explicit-import MVP complete)
- `import name(arity)` declares native host functions registered through
  `dao_host_function` (guide §10/§11), and calls lower to `CALL_HOST`.
- Stable FNV-1a symbol IDs, duplicate rejection, signature checks, and internal/host name
  conflict checks are enforced at compile time.

### Phase M — representative samples (MVP complete)
- `kernel/stdlib/core.ku` provides migrated math/list/string behavior and explicit
  io/fs/http/debug host boundaries.
- `kernel/tests/fixtures/language_acceptance.ku` covers nested containers, loops, math,
  strings, and protected exceptions. Both files compile through `dao-ku` in CTest.

### Phase V — MVP acceptance (full legacy parity pending)
- Native execution covers arithmetic, comparisons, Trit logic, calls, host imports,
  branches, loops, break/continue, and compile-time rejection cases.
- Every generated module passes loader/verifier and assembler byte-identity round-trip.
- The benchmark exceeds the 10 M typed ops/s gate, and `dao-ku` provides the file CLI.

## Remaining For Full K4 Closure

- legacy `引 "std/..." 别 ...` import compatibility;
- real `ku/std/*.ku` migration instead of representative replacements;
- function references/closures, floats, and remaining dynamic semantics;
- parity against the complete legacy Python/Ku corpus;
- stable, benchmark-selected owned container ABI.

The `.ku` self-hosting replacement is tracked in `docs/KU_SELFHOST.md`. SH0 is complete;
SH1 parser/compiler parity and SH2 byte-identical self-rebuild remain pending.

## Non-negotiable (per guide §12 / §14)
- Must go through typed builder; no offset-patching or handwritten binary in the hot path.
- Round-trip byte-identity is the correctness criterion.
- Strict-validate every field; no silent discard (extreme-value anchor principle).
- Author in C++ inside the existing CMake build; no Python fallback on the production path.
