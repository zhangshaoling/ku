# Dao Binary Module v1/v2/v3 and Register Bytecode ABI v9/v10/v11

All multibyte values use little-endian encoding. Offsets are relative to the start of the module.

## Header

Size: 16 bytes.

| Offset | Size | Field | accepted value |
| ---: | ---: | --- | --- |
| 0 | 4 | magic | `44 41 4f 00` (`DAO\0`) |
| 4 | 2 | format version | `1`, `2`, or opt-in `3` |
| 6 | 2 | VM ABI version | `9`, `10`, or opt-in `11` |
| 8 | 4 | flags | `0` |
| 12 | 4 | section count | `5`, `7`, or `9` |

Accepted pairs are v1/ABI9, v2/ABI10 and opt-in v3/ABI11. Cross-paired or unknown versions and
nonzero flags are rejected.

v3/ABI11 is supported by the C++ builder, runtime C ABI, both Ku source frontends and
assembly/disassembly libraries. Its Record
sections/instructions, capability mask and limits are defined in
[the R0 protocol](../docs/KU_RECORD_R0_ABI.md); implementation scope is in [RECORDS.md](RECORDS.md).
Default VMs reject v3 with `DAO_UNSUPPORTED_FEATURE` until explicitly enabled.

### ABI11 assembly text

Canonical v3 text starts with `format: 3 abi=11`, followed by the existing optional
`module:` / `module_imports:` identity lines, then imports, exports and hex strings.
The `record_types:` section precedes `functions:`. Names reference DATA indices;
field types are numeric tags I64=1, Trit=2, Bytes=3 and String=4.

```text
format: 3 abi=11
imports: 0
exports: 1
  symbol=1 -> function 0
strings: 2
  hex=506f696e74
  hex=78
record_types: 1
  [record 0] name=0 fields=1
    field name=1 type=1
functions: 1
  [function 0] registers=3 parameters=0 instructions=4
    0 LOAD_I64 r0, 42
    1 MAKE_RECORD r1, r0, 1, 0
    2 RECORD_GET r2, r1, 0, 0
    3 RETURN r2
```

`MAKE_RECORD dst, base, count, type_index`, `RECORD_GET dst, object, slot, type_index`
and `FIELD_GET dst, object, 0, name_index` use register notation only for dst and
object/base; the final operands are unsigned integers. Declaration order determines
type indices and field slots. Empty types use `fields=0`. Counts, names, scalar tags
and instruction operands must satisfy the same ABI11 verifier as executable modules.

Both parsed/disassembled C++ models retain format/ABI versions and Record tables.
Canonical ModuleBuilder output round-trips byte for byte, including an optional module
identity and imports, shared DATA names and unused strings. Arbitrary alternative
binary layouts are normalized by the builder. v1/v2 text and lowering remain unchanged.
Disassembly and text assembly verify v3 using a temporary VM with Records enabled;
this does not link dependencies, run code or enable any application VM. Numeric AOT
rejects every v3 module, including one with no Record instructions, before opening output.

## Section Table

Each entry is 16 bytes:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | section type |
| 4 | 4 | byte offset |
| 8 | 4 | byte size |
| 12 | 4 | record count |

Initial section types:

| ID | Section | Record size |
| ---: | --- | ---: |
| 1 | `FUNC` | 16 |
| 2 | `CODE` | 16 |
| 3 | `EXPORT` | 8 |
| 4 | `IMPORT` | 8 |
| 5 | `DATA` | variable |
| 6 | `METADATA` | 24 |
| 7 | `MODULE_IMPORT` | 24 |
| 8 | `RECORD_TYPE` (ABI11) | 16 |
| 9 | `RECORD_FIELD` (ABI11) | 12 |

Sections must lie after the section table, remain inside the module, and not overlap.
Duplicate or unknown section types are rejected. VM ABI v9 requires the first five sections;
VM ABI v10 additionally requires one `METADATA` record and a `MODULE_IMPORT` section.
`IMPORT`, `MODULE_IMPORT`, and `DATA` may contain zero dependency/data records as applicable.
ABI11 requires the five core sections plus RECORD_TYPE/FIELD; identified ABI11 modules
also contain METADATA/MODULE_IMPORT. These new sections are rejected in older formats.

## Module Metadata Record (v2)

Size: 24 bytes. It contains a `DATA` string index for the non-empty logical identity,
`uint32` major/minor/patch version fields, and two zero reserved fields. Identity is opaque
UTF-8, not a filename and not the content fingerprint.

## Module Import Record (v2)

Size: 24 bytes: dependency identity `DATA` index; exact `uint32` major/minor/patch version;
exported symbol ID; `uint16` parameter count; zero `uint16` reserved field. Record order is
addressed by `CALL_MODULE` immediates.

## Runtime Value ABI

`dao_value` remains a fixed 16-byte C ABI value:

| Offset | Size | Field | Scalar | Borrowed view |
| ---: | ---: | --- | --- | --- |
| 0 | 4 | type | `NULL/I64/TRIT/LIST/MAP` | `BYTES/STRING` |
| 4 | 4 | reserved | zero | byte length |
| 8 | 8 | payload | scalar payload | pointer encoded through `intptr_t` |

Borrowed views are limited to `UINT32_MAX` bytes so the register value stays 16 bytes. A nonempty view requires a non-null pointer. `STRING` must contain strict UTF-8: overlong encodings, surrogate code points, invalid continuation bytes, truncation, and values above `U+10FFFF` are rejected. The VM never copies, owns, frees, or extends the lifetime of view storage.

## Data String Record

The `DATA` section begins with `count` 8-byte records containing a section-relative
`uint32` byte offset and `uint32` byte length. Payload bytes follow the records. Ranges
must remain inside `DATA`, may not point into the record table, and must be strict UTF-8.
The loader copies strings into immutable module-owned storage.

## Import Record

Size: 8 bytes.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | numeric host symbol ID |
| 4 | 2 | parameter count |
| 6 | 2 | reserved, zero |

Import records use declaration order because bytecode addresses them by zero-based import index. Duplicate symbols are rejected. Modules contain no host address, function pointer, name, or serialized host state.

## Function Record

Size: 16 bytes.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | first instruction index in `CODE` |
| 4 | 4 | instruction count |
| 8 | 2 | register count |
| 10 | 2 | parameter count |
| 12 | 4 | reserved, zero |

Parameters arrive in registers `r0..r(parameter_count-1)`. A function must execute `RETURN`; falling off the end is a runtime error.

## Instruction Record

Size: 16 bytes.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | numeric opcode |
| 1 | 1 | flags, zero in v1 |
| 2 | 2 | destination register |
| 4 | 2 | operand/register `a` |
| 6 | 2 | operand/register `b` |
| 8 | 8 | signed immediate |

Branch targets are function-local instruction indexes.

## Opcodes

| ID | Name | Semantics |
| ---: | --- | --- |
| 0 | `NOP` | advance |
| 1 | `LOAD_I64` | `dst = immediate` |
| 2 | `MOVE` | `dst = a` |
| 3 | `ADD_I64` | checked `dst = a + b` |
| 4 | `SUB_I64` | checked `dst = a - b` |
| 5 | `MUL_I64` | checked `dst = a * b` |
| 6 | `DIV_I64` | checked `dst = a / b` |
| 7 | `TRIT_NOT` | `dst = -a` |
| 8 | `TRIT_AND` | `dst = min(a,b)` |
| 9 | `TRIT_OR` | `dst = max(a,b)` |
| 10 | `BR_TRIT_NEG` | jump to immediate when `a < 0` |
| 11 | `BR_TRIT_ZERO` | jump to immediate when `a == 0` |
| 12 | `BR_TRIT_POS` | jump to immediate when `a > 0` |
| 13 | `JUMP` | unconditional jump to immediate |
| 14 | `CALL` | call function `immediate`, args start at `a`, count `b`, result to `dst` |
| 15 | `RETURN` | return register `a` |
| 16 | `CALL_HOST` | call import `immediate`, args start at `a`, count `b`, result to `dst` |
| 17 | `LOAD_TRIT` | load `-1`, `0`, or `+1` into `dst` |
| 18 | `REM_I64` | integer remainder `dst = a % b` |
| 19 | `EQ_I64` | integer equality, returning Trit true/false |
| 20 | `NE_I64` | integer inequality, returning Trit true/false |
| 21 | `LT_I64` | integer less-than, returning Trit true/false |
| 22 | `LE_I64` | integer less-than-or-equal, returning Trit true/false |
| 23 | `GT_I64` | integer greater-than, returning Trit true/false |
| 24 | `GE_I64` | integer greater-than-or-equal, returning Trit true/false |
| 25 | `LOAD_STRING` | load a module-owned UTF-8 constant |
| 26 | `MAKE_LIST` | construct a VM-owned list from consecutive registers |
| 27 | `LEN` | return list or map length as i64 (`LIST_LEN` is accepted as a compatibility spelling) |
| 28 | `LIST_GET` | read list element by i64 index |
| 29 | `MAKE_MAP` | construct a string-keyed map from key/value register pairs |
| 30 | `INDEX_GET` | dynamically index a list or map |
| 31 | `TRY_BEGIN` | push a function-local exception handler target |
| 32 | `TRY_END` | pop the active exception handler |
| 33 | `THROW` | propagate a value to the nearest handler |
| 34 | `CATCH` | load the current caught value |
| 35 | `LOAD_NULL` | load the typed null value |
| 36 | `INDEX_SET` | mutate a VM-owned list element or string-keyed map entry |
| 37 | `LIST_APPEND` | append register `a` to VM-owned list register `dst` |
| 38 | `LOAD_FUNCTION` | load same-module function `immediate` into `dst` |
| 39 | `CALL_VALUE` | call function reference `a`, args start at `b`, count `immediate`, result to `dst` |
| 40 | `MAKE_CLOSURE` | bind registers `a..a+b-1` to same-module function `immediate` in `dst` |
| 41 | `CALL_MODULE` | call an identified linked module export |
| 42 | `MAP_KEYS` | return a List of string keys for a Map |
| 43 | `MAKE_RECORD` (ABI11) | construct type `immediate` from `b` scalar registers starting at `a` into `dst` |
| 44 | `RECORD_GET` (ABI11) | read slot `b` from Record `a`, checking local type `immediate`, into `dst` |
| 45 | `FIELD_GET` (ABI11) | read named field (DATA `immediate`) from Map/Record `a` into `dst`; b is zero |
| 41 | `CALL_MODULE` | call module import `immediate`, args start at `a`, count `b`, result to `dst` |

Arithmetic requires `i64`. Trit operations and branches require payload `-1`, `0`, or `+1`.
`CALL_MODULE` requires an exact linked identity/version/export/signature and rejects
module-local Function/Closure values at the boundary. Type mismatches trap with a structured
status. The complete link contract is in [`MODULE_ABI.md`](MODULE_ABI.md).

`CALL_HOST` requires its argument count to match both the import record and the registered host function. Host callbacks use the C ABI directly; callback results are validated before entering a VM register. Missing imports trap with `DAO_IMPORT_NOT_FOUND`.

## Export Record

Size: 8 bytes.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | numeric symbol ID |
| 4 | 4 | function index |

Exports are encoded in ascending symbol order for deterministic modules. Duplicate symbols are rejected.

## Execution Limits

The host config controls:

- maximum module bytes
- maximum registers per function
- maximum call depth
- maximum instructions per top-level call

Instruction budget is shared by nested calls. These limits are part of host policy, not module semantics.

## Versioning

VM ABI11 adds opt-in scalar Record descriptors, MAKE_RECORD/RECORD_GET/FIELD_GET and
read-only C inspection. It uses format v3, allows optional identity metadata, and keeps
the 16-byte value layout. Existing module formats and default VM feature masks are preserved.

Changing an opcode's meaning, record layout, register convention, or value ABI requires a VM ABI version change. VM ABI v2 added `IMPORT` and `CALL_HOST`. VM ABI v3 added borrowed views. VM ABI v4 added Trit constants, remainder, and comparisons. VM ABI v5 added module DATA constants, initial containers, indexing, and structured exceptions. VM ABI v6 replaces container pointers with generation handles and adds `INDEX_SET`. VM ABI v7 adds `LIST_APPEND`. VM ABI v8 adds local function references and `CALL_VALUE`. VM ABI v9 adds same-call captured bindings. VM ABI v10 adds identified module metadata, exact-version module imports, and `CALL_MODULE`. The loader retains v1/ABI9 compatibility; other older or cross-paired modules are rejected rather than guessed.
