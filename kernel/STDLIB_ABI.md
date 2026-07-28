# Ku Standard Library Module ABI

Status: version matrix for the migrated Ku v1 standard-library subset.

All logical module identities below use exact version `1.0.0`. Changing a listed function
arity or cross-module value contract requires a new module version. Host capability imports
remain numeric `IMPORT`/`CALL_HOST` entries and are not module identities.

## Identity and dependency matrix

| Identity | Exact runtime dependencies |
|---|---|
| `ku:std/core@1.0.0` | none |
| `ku:std/math@1.0.0` | none |
| `ku:std/list@1.0.0` | `ku:std/type@1.0.0`: `is_list/1` |
| `ku:std/type@1.0.0` | none |
| `ku:std/fs@1.0.0` | none |
| `ku:std/io@1.0.0` | `ku:std/fs@1.0.0`: `exists/1`, `read_text/1`, `write_text/2`, `read_or/2`, `copy_file/2`, `safe_delete/1`; `ku:std/string@1.0.0`: `length/1`, `concat/2`, `not_empty/1` |
| `ku:std/debug@1.0.0` | none |
| `ku:std/string@1.0.0` | none |
| `ku:std/http@1.0.0` | none |
| `ku:std/lexer@1.0.0` | `ku:std/string@1.0.0`: `length/1`, `char_at/2`, `substring/3`, `concat/2`, `ord/1`, `chr/1` |

`list`, `io`, and `lexer` use `MODULE_IMPORT`/`CALL_MODULE`; they do not resolve these
dependencies from source. Missing providers return `DAO_IMPORT_NOT_FOUND` at the call site.
`lexer.ku` still uses a legacy multiline-list syntax point and is compiled explicitly with
`dao-ku --recovery`; its emitted identity and module ABI are the same v2/ABI10 contract.

## Export signatures

The stable signature is `name/arity`:

- `core`: `math_abs/1`, `list_sum/1`, `string_identity/1`, `io/1`, `fs/1`, `http/1`, `debug/1`.
- `math`: `abs/1`, `sum/1`, `product/1`, `avg/1`, `clamp/3`, `lerp/3`, `gcd/2`, `lcm/2`, `is_prime/1`, `fibonacci/1`.
- `list`: `includes/2`, `first/1`, `last/1`, `count/2`, `min_of/1`, `max_of/1`, `slice/3`, `reverse/1`, `unique/1`, `sort/1`, `zip/2`, `enumerate/1`, `map/2`, `filter/2`, `reduce/3`, `find/2`, `all/2`, `any/2`, `flat_map/2`, `flatten/1`, `group_by/2`, `interleave/2`, `step/2`, `pad/3`, `rotate/2`.
- `type`: `type_of/1`, `is_null/1`, `is_num/1`, `is_string/1`, `is_list/1`, `is_map/1`.
- `fs`: `exists/1`, `read_text/1`, `write_text/2`, `ensure_dir/1`, `read_or/2`, `copy_file/2`, `safe_delete/1`.
- `io`: `file_exists/1`, `file_size/1`, `append_file/2`, `append_line/2`, `read_or/2`, `copy_file/2`, `safe_delete/1`.
- `debug`: `trace/1`, `timer_start/0`, `timer_elapsed/1`, `measure/1`, `assert/2`, `assert_eq/2`, `memory_usage/0`.
- `string`: `length/1`, `is_empty/1`, `not_empty/1`, `trim/1`, `upper/1`, `lower/1`, `replace/3`, `contains/2`, `starts_with/2`, `ends_with/2`, `substring/3`, `char_at/2`, `concat/2`, `ord/1`, `chr/1`.
- `http`: `get/1`, `post/3`, `is_ok/1`, `is_error/1`.
- `lexer`: `make_token/4`, `is_digit/1`, `is_letter/1`, `is_id_start/1`, `is_id_char/1`, `is_keyword/1`, `lex_number/4`, `lex_identifier/4`, `lex_string/4`, `lex_operator/4`, `next_token/4`, `lex/1`.

## Value boundary

Module calls may pass scalars, borrowed Bytes/String views, and current-generation List/Map
values. Function and Closure values are rejected, including inside containers.

Consequences for this version:

- the normal `list -> type`, `io -> fs/string`, and `lexer -> string` paths only pass safe
  values;
- `io.read_or/2` and `fs.read_or/2` accept only values allowed by the module boundary;
  a Function/Closure default returns `DAO_TYPE_ERROR`;
- callback-taking `list.map/2`, `filter/2`, `reduce/3`, `find/2`, `all/2`, `any/2`,
  `flat_map/2`, `group_by/2`, and `debug.measure/1` remain source-composition APIs. Their
  callback argument cannot cross the stable module ABI in v1.

The binary format currently exports every top-level `thought`; the list above defines the
contractual surface. A future private/export declaration may hide implementation helpers,
but must not silently change these versioned signatures.
