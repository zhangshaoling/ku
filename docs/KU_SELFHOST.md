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
Single-level identifier-backed list and map indexing supports `container[key] = value` through
`INDEX_SET`.
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
`throw`, `try`, `catch`, `in`) uses the `is_kw` helper that compares token
length and source bytes directly, returning VM trits for use in `and`/`or`
chains. The byte-scanning `advance_to_byte` bridge has been removed; `compile`
passes token indices to `parse_body` and reads back the consumed token index
from the packed result. Function/import lookup and arity validation also traverse
the shared token stream; numeric import arities are read from number tokens.

This remains deliberately bounded: register-overflow checks, nested indexed
mutation, module-path imports, and canonical self-rebuild are still pending. Diagnostics
now cover explicit `throw` paths (unterminated string/list/map literals, missing
function body braces, empty number consumption, host/local arity mismatch,
undefined identifiers, unexpected characters) but do not yet carry source
positions.

## SH1: Real Frontend Remaining

- attach token offsets to diagnostics and reject duplicate declarations;
- support module-path imports and nested indexed assignment;
- add register-allocation bounds and structured builder validation;
- compare SH1 bytes with the C++ recovery compiler for canonical identity.

## SH2: Self-Rebuild

- compile the `.ku` compiler with its previous verified binary;
- compile it again with the newly produced binary;
- require byte-identical compiler modules and parity corpus results;
- retain the C++ compiler only as recovery/bootstrap tooling.
