# Dao Binary Module v1 and Register Bytecode ABI v3

All multibyte values use little-endian encoding. Offsets are relative to the start of the module.

## Header

Size: 16 bytes.

| Offset | Size | Field | v1 value |
| ---: | ---: | --- | --- |
| 0 | 4 | magic | `44 41 4f 00` (`DAO\0`) |
| 4 | 2 | format version | `1` |
| 6 | 2 | VM ABI version | `3` |
| 8 | 4 | flags | `0` |
| 12 | 4 | section count | `4` |

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

Sections must lie after the section table, remain inside the module, and not overlap. Duplicate or unknown section types are rejected. VM ABI v3 requires all four sections; `IMPORT` may contain zero records.

## Runtime Value ABI

`dao_value` remains a fixed 16-byte C ABI value:

| Offset | Size | Field | Scalar | Borrowed view |
| ---: | ---: | --- | --- | --- |
| 0 | 4 | type | `NULL/I64/TRIT` | `BYTES/STRING` |
| 4 | 4 | reserved | zero | byte length |
| 8 | 8 | payload | scalar payload | pointer encoded through `intptr_t` |

Borrowed views are limited to `UINT32_MAX` bytes so the register value stays 16 bytes. A nonempty view requires a non-null pointer. `STRING` must contain strict UTF-8: overlong encodings, surrogate code points, invalid continuation bytes, truncation, and values above `U+10FFFF` are rejected. The VM never copies, owns, frees, or extends the lifetime of view storage.

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

Changing an opcode's meaning, record layout, register convention, or value ABI requires a VM ABI version change. VM ABI v2 added `IMPORT` and `CALL_HOST`. VM ABI v3 adds the 16-byte borrowed `BYTES` and UTF-8 `STRING` value representations. Older modules are intentionally rejected rather than guessed. Adding an optional container feature requires a declared flag and compatible loader behavior.
