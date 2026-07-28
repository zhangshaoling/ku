# K4 legacy `.ku` migration compiler — staged plan

## Background
- Ku = self-hosted language (`ku/`): lexer, parser, runtime, stdlib, macro (`thought`), import system.
- Old compiler (`ku/compiler.py`) emits a Python-executable bytecode for the stack VM. Per guide §15 this is migration **input**, not something to extend.
- New target: Dao Binary Module v1 + `ModuleBuilder` typed builder + the versioned
  register opcode ABI (currently VM ABI v9).
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
- Implemented by VM ABI v9: module-owned UTF-8 string constants, lists, string-keyed maps,
  dynamic list/map indexing, verified list append, local function references, and same-call bindings.
  Missing map keys evaluate to `null`, matching the legacy runtime and C-VM parity contract;
  equality/inequality accepts `null`, Trits, borrowed strings/bytes, and cross-type inequality
  without weakening ordered integer comparisons or enabling container deep equality.
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
- Legacy source imports (`引 "std/..." 别 alias` and `import "std/..." as alias`) are
  resolved explicitly by `dao-ku`, recursively source-bundled under deterministic alias
  prefixes, cycle-checked, and confined to the input module root.

### Phase M — representative samples (MVP complete)
- `kernel/stdlib/core.ku` provides migrated math/list/string behavior and explicit
  io/fs/http/debug host boundaries.
- `kernel/tests/fixtures/language_acceptance.ku` covers nested containers, loops, math,
  strings, and protected exceptions. Both files compile through `dao-ku` in CTest.
- `kernel/stdlib/math.ku` is a direct migration of the integer/list-compatible functions
  from `ku/std/math.ku`, exercised through legacy module import and VM execution.
- `kernel/stdlib/list.ku` migrates reads, membership, counting, extrema, list-building, and
  higher-order helpers from `ku/std/list.ku`. The migrated integer/list subset now includes
  flat-map, group-by, interleave, step, pad, and positive/negative rotation; cross-call
  persistent closures remain pending.
- `kernel/stdlib/fs.ku` migrates filesystem capability wrappers for existence checks,
  text reads/writes, directory creation, fallback reads, copies, and safe deletion. OS access
  remains behind explicit fixed-arity Host imports and is tested with an in-memory Host.
- `kernel/stdlib/debug.ku` migrates scalar logging, explicit timer tokens, function
  measurement, assertions, and memory counters. Clock, logging, and process metrics remain
  explicit Host capabilities; dynamic recursive object inspection still requires type reflection.
- `kernel/stdlib/string.ku` migrates length/empty checks, trim, ASCII case conversion,
  replacement, containment, prefix/suffix checks, substring, and indexed character access.
  Concatenation is also exposed for higher-level modules. Borrowed results are supplied by
  explicit `host_string_*` capabilities with stable storage.
- `kernel/stdlib/io.ku` composes the migrated `fs` and `string` modules for file-size,
  append, append-line, fallback read, copy, and safe-delete operations. Its tests exercise
  recursive standard-library import rewriting against the same in-memory Host filesystem.
- `kernel/stdlib/http.ku` migrates GET/POST capability wrappers and response-status helpers.
  Request header Maps are created in `.ku` and inspected by the Host through the public
  container API; JSON decoding and Host response-map construction remain module/adapter work.
- `kernel/stdlib/lexer.ku` migrates executable ASCII tokenization on top of the `string`
  module, including keywords, identifiers, integers, strings, operators, punctuation,
  newlines, and `;;` comments. Tests inspect every returned token Map and EOF marker;
  full UTF-8 codepoint iteration remains pending.
- `kernel/stdlib/type.ku` exposes dynamic value reflection through the explicit
  `host_value_type(1)` capability for all nine current ABI value tags. Its predicates enable
  recursive standard-library behavior without changing the frozen VM ABI; `list.flatten`
  now recursively flattens nested Lists using this capability.

### Phase V — MVP acceptance (full legacy parity pending)
- Native execution covers arithmetic, comparisons, Trit logic, calls, host imports,
  branches, loops, break/continue, and compile-time rejection cases.
- Every generated module passes loader/verifier and assembler byte-identity round-trip.
- The benchmark exceeds the 10 M typed ops/s gate, and `dao-ku` provides the file CLI.

### Legacy syntax convergence
- Both migration frontends accept `;;` line comments.
- The C++ recovery/migration parser normalizes prefix operator calls such as `>= (a, b)`,
  variadic `and/or`, multiline call/list/map literals, same-line bare alternate blocks,
  and value-producing `if (...) { value } { alternate }` expressions onto existing AST
  and VM instructions.
- Parenthesized operands in statement conditions, such as `(a) or (b)`, remain part of the
  full expression instead of being mistaken for a wrapper around the complete condition.
- Legacy `push(list, value)` lowers directly to verified `LIST_APPEND`.
- Legacy `parser.ku` now compiles as an integer-compatible module with explicit
  `host_value_type(1)` and `host_string_to_i64(1)` capabilities. Its indexed-assignment
  condition had one unmatched parenthesis, now covered by clean-build compilation and
  execution CTests. Assignment, binary-expression, literal, and indexed-assignment AST
  paths execute through VM-owned Maps/Lists. Decimal parsing remains pending because frozen
  ABI v9 has no float value tag.
- Legacy `task_queue.ku` now compiles without shell or Python subprocesses. Database-path,
  formatted-time, task-ID, and fixed-arity SQLite operations are explicit Host capabilities;
  routing decisions execute natively and are covered for all configured routes. The claim path
  executes against Host-created `List<Map>` query rows through the stable generation-scoped
  container ABI.
  The lexer has moved to the migrated executable module described above.

## Remaining For Full K4 Closure

- remaining `ku/std/*.ku` modules and dynamic semantics beyond the migrated integer/list math subset;
- persistent cross-call closures, floats, and remaining dynamic semantics;
- parity against the complete legacy Python/Ku corpus;
- alignment with the authoritative `KU_V1_SEMANTICS.md` matrix.

The `.ku` self-hosting replacement is tracked in `docs/KU_SELFHOST.md`. SH2 now passes
functional rebuild, consecutive byte-identical rebuild, and rebuilt-compiler parity for
the current frontend surface, including recursive compile-time module imports. `dao-ku`
boots that frontend by default; `--recovery` is an explicit path for compatibility-only
sources and is never selected as an automatic fallback. Full legacy K4 parity and the
remaining SH1 diagnostics/builder surface are still pending.

## Non-negotiable (per guide §12 / §14)
- Must go through typed builder; no offset-patching or handwritten binary in the hot path.
- Round-trip byte-identity is the correctness criterion.
- Strict-validate every field; no silent discard (extreme-value anchor principle).
- Author in C++ inside the existing CMake build; no Python fallback on the production path.
