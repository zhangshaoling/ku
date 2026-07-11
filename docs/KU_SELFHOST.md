# Ku Self-Hosting Path

The final language frontend belongs in `.ku`; the loader, verifier, VM, C ABI, cache,
and native backend remain in C/C++.

## SH0: Seed Compiler (implemented)

`kernel/selfhost/compiler.ku` is compiled by the recovery/bootstrap C++ frontend. SH0
established the path that receives source as a UTF-8 view, reads source bytes in `.ku`,
and emits a minimal single-function integer module through numeric typed-builder Host
functions. The returned bytes are loaded, verified, and executed by the normal VM.

`ku_selfhost_seed` proves this chain:

```text
compiler.ku -> bootstrap .dao -> execute compile(source)
            -> typed builder host ABI -> generated .dao
            -> loader/verifier -> execute main() -> 42
```

C++ in this path is a host adapter for `ModuleBuilder`, not the language parser.

## SH1a: Expression Frontend Slice (implemented)

The first SH1 slice keeps the one-function numeric scope but moves expression handling
into explicit `.ku` parser functions. It now supports whitespace skipping, primary
parsing, parentheses, unary minus, and precedence-separated additive/multiplicative
operators.

This is intentionally still a seed path, not the final frontend. It does not yet support
identifiers, variables, multiple functions, function calls, string/list/map syntax,
imports, proper diagnostics, register-overflow checks, or canonical self-rebuild.

## SH1b: Multi-Function Expression Modules (implemented)

The `.ku` compiler now scans and emits consecutive `thought` declarations into one generated
module, preserving each source function name as an export. Parameters and identifier reads
lower directly to parameter registers. Function bodies support semicolon-separated local
assignments, and a function can call another generated function with zero or more integer
expression arguments. The compiler preserves argument values and packs them into the
contiguous register range required by the VM call ABI. Reassignment writes back to an existing
local binding's register. Integer comparisons
(`==`, `!=`, `<`, `<=`, `>`, and `>=`) lower to VM trits.
Whitespace-delimited `if <condition> { ... }` blocks and explicit `return <expression>` statements
emit and patch VM branch targets in the self-hosted compiler. `else { ... }` produces the
matching branch-over-alternate jump. Basic `while <condition> { ... }` loops re-evaluate their
condition and jump back through the self-hosted builder ABI. `break` and `continue` are patched
within their enclosing loop range.
String literals and escape semantics are handled in `.ku`. Generic `string_new`,
`string_append`, and `string_finish` Host primitives only provide byte storage. Supported
escapes are `\n`, `\t`, `\r`, `\"`, `\\`, and `\0`; any other `\x` lowers to the literal byte
`x`. Unterminated string literals raise a `throw`.
List literals preserve nested expression results before packing them into `MAKE_LIST`; `len(x)`
lowers to `LIST_LENGTH`, and an identifier-backed list supports `items[index]` through
`INDEX_GET`. `for item in list { ... }` lowers to list length, indexed element reads, and a
cursor loop; its `continue` target advances the cursor before retesting.
Map literals preserve string-key/value pairs before packing them into `MAKE_MAP`; existing
identifier bindings use `INDEX_GET` for string-key lookup.
Identifier-backed list and map indexing supports `container[key] = value` through
`INDEX_SET`. Nested assignment chains such as `matrix[row][column] = value` resolve
intermediate containers with `INDEX_GET` before mutating the final container.
`throw <expression>` and `try { ... } catch error { ... }` lower to patched VM handler targets;
the catch variable receives the thrown value.
Top-level `import name(arity)` declarations are registered before functions; matching named
calls lower to `CALL_HOST` while local function indices exclude imports.

The `compile` top-level loop and all downstream parsers (`parse_body`,
`parse_expression`, `parse_additive`, `parse_multiplicative`, `parse_primary`,
`parse_number`, `parse_string`, `parse_list`, `parse_map`) now consume a token
stream produced by the self-hosted `tokenize` function. Tokens are encoded as
i64 values (`type * 10^12 + start * 10^6 + length`) covering 26 token types:
numbers, strings, identifiers, delimiters, operators, and EOF. Keyword
recognition (`if`, `else`, `while`, `for`, `break`, `continue`, `return`,
`throw`, `try`, `catch`, `in`, `not`, `and`, `or`, `true`, `false`, `null`)
uses the `is_kw` helper that compares token
length and source bytes directly, returning VM trits for use in `and`/`or`
chains. Logical negation and trit conjunction/disjunction lower to the matching
VM instructions. Line feeds are emitted as statement-separator tokens, so
newline-delimited `.ku` source and semicolon-delimited source follow the same
parser path. The byte-scanning `advance_to_byte` bridge has been removed; `compile`
passes token indices to `parse_body` and reads back the consumed token index
from the packed result. Function/import lookup and arity validation also traverse
the shared token stream; numeric import arities are read from number tokens.
Before code generation, a token preflight rejects duplicate functions, duplicate imports,
import/function name conflicts, and duplicate parameters.
All emitted instructions pass through a `.ku` register-boundary wrapper before reaching the
raw builder Host adapter; register indices and contiguous counts must remain below the VM's
4096-register default. Packed parser results use widened fields so token and register indices
do not truncate during the self-rebuild. New local bindings receive independent registers,
preventing assignments such as `start = index` from aliasing later writes to `index`.

This remains deliberately bounded: module-path imports are still pending. Diagnostics
cover explicit `throw` paths (unterminated string/list/map literals, missing function
body braces, empty number consumption, host/local arity mismatch, undefined identifiers,
and unexpected characters). The `.ku` frontend now constructs source-positioned messages
for lexer failures and top-level/expression entry errors, and uncaught string exceptions
are preserved through `dao_error.message`. Remaining parser diagnostics still need token
offsets.

## SH1: Real Frontend Remaining

- attach token offsets to the remaining parser diagnostics;
- support module-path imports;
- expand structured builder validation beyond register bounds;
- compare SH1 bytes with the C++ recovery compiler for canonical identity.

## SH2: Canonical Self-Rebuild (implemented)

`ku_selfhost_seed` now compiles `compiler.ku` with the bootstrap-produced compiler,
loads that rebuilt compiler, and uses it to compile and execute a fresh module whose
result must be `42`. This proves the generated compiler is loadable and functionally
capable of compiling downstream `.ku` source. The test uses an elevated instruction
budget because the current token lookup and code generation paths are intentionally
simple and not yet optimized.

The rebuilt compiler also compiles `compiler.ku` again. The first and second rebuilt
modules must have identical sizes and byte-for-byte contents, proving that self-rebuild
has reached a deterministic fixed point.

The rebuilt compiler runs the same positive parity matrix as the bootstrap compiler,
covering arithmetic, precedence, local and inter-function calls, comparisons, trit
logic, null, control flow, strings and escapes, lists, maps, indexing, and iteration.
Host imports, indexed mutation, exception recovery, malformed-source rejection,
duplicate declarations and parameters, and register overflow are also checked through
both compiler generations.

This closes SH2 acceptance for the current self-hosted frontend surface. The C++ frontend
remains recovery/bootstrap tooling. Full legacy-language parity is a separate K4 closure
item tracked in `KU_MIGRATION_PLAN.md`.
