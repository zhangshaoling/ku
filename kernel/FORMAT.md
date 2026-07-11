# Dao Binary Module v1 and Register Bytecode ABI v5

All multibyte values use little-endian encoding. Offsets are relative to the start of the module.

## Header

Size: 16 bytes.

| Offset | Size | Field | v1 value |
| ---: | ---: | --- | --- |
| 0 | 4 | magic | `44 41 4f 00` (`DAO\0`) |
| 4 | 2 | format version | `1` |
| 6 | 2 | VM ABI version | `5` |
| 8 | 4 | flags | `0` |
| 12 | 4 | section count | `5` |

Unknown versions or nonzero v1 flags are rejected.

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

Sections must lie after the section table, remain inside the module, and not overlap. Duplicate or unknown section types are rejected. VM ABI v5 requires all five sections; `IMPORT` and `DATA` may contain zero records.

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
| 27 | `LIST_LEN` | return list length as i64 |
| 28 | `LIST_GET` | read list element by i64 index |
| 29 | `MAKE_MAP` | construct a string-keyed map from key/value register pairs |
| 30 | `INDEX_GET` | dynamically index a list or map |
| 31 | `TRY_BEGIN` | push a function-local exception handler target |
| 32 | `TRY_END` | pop the active exception handler |
| 33 | `THROW` | propagate a value to the nearest handler |
| 34 | `CATCH` | load the current caught value |
| 35 | `LOAD_NULL` | load the typed null value |

Arithmetic requires `i64`. Trit operations and branches require payload `-1`, `0`, or `+1`. Type mismatches trap with a structured status.

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

Changing an opcode's meaning, record layout, register convention, or value ABI requires a VM ABI version change. VM ABI v2 added `IMPORT` and `CALL_HOST`. VM ABI v3 added borrowed views. VM ABI v4 added Trit constants, remainder, and comparisons. VM ABI v5 adds module DATA constants, VM-owned containers, indexing, and structured exceptions. Older modules are intentionally rejected rather than guessed.
