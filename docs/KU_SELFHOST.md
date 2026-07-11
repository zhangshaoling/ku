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
Plain string literals are copied into the generated module data section and loaded with
`LOAD_STRING` (escape sequences are not yet parsed).
List literals preserve nested expression results before packing them into `MAKE_LIST`; `len(x)`
lowers to `LIST_LENGTH`, and an identifier-backed list supports `items[index]` through
`INDEX_GET`. `for item in list { ... }` lowers to list length, indexed element reads, and a
cursor loop; its `continue` target advances the cursor before retesting.

This remains deliberately bounded: token streams, string escapes, containers, imports,
diagnostics, register-overflow checks, list mutation, maps, and canonical self-rebuild are still
pending.

## SH1: Real Frontend Remaining

- move tokenization into `.ku` modules and replace byte-scanning with token streams;
- expose string primitives and structured builder operations through numeric Host ABI;
- compile functions, control flow, imports, constants, and containers;
- compare SH1 bytes with the C++ recovery compiler for canonical identity.

## SH2: Self-Rebuild

- compile the `.ku` compiler with its previous verified binary;
- compile it again with the newly produced binary;
- require byte-identical compiler modules and parity corpus results;
- retain the C++ compiler only as recovery/bootstrap tooling.
