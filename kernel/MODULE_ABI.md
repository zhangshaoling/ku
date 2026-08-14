# Ku Stable Module ABI

Status: v1 module-link contract, implemented by Dao Binary Module format v2 / VM ABI v10.

## Identity

An identified module owns an explicit tuple:

```text
(UTF-8 logical name, version major, version minor, version patch)
```

The logical name is opaque to the kernel, non-empty, and at most 1024 bytes. It may name a
standard-library API, a persisted Thought, or a memory-derived program; it is not inferred
from a filename. Versions are matched exactly. Range selection and package repositories are
policy layers above this ABI.

`dao_module_fingerprint` remains a content-cache hint. A fingerprint is not a logical
identity, and cache lookup still confirms full byte equality. Linking two different byte
sequences under the same identity/version returns `DAO_MODULE_CONFLICT`.

## Binary contract

Legacy format v1 / VM ABI v9 modules remain loadable but have no logical identity and cannot
be linked as module dependencies. Format v2 / VM ABI v10 adds:

- one `METADATA` record containing the identity and exact version;
- zero or more `MODULE_IMPORT` records containing dependency identity/version, exported
  symbol ID, and fixed parameter count;
- `CALL_MODULE`, whose immediate is a module-import index.

Module names are immutable UTF-8 entries in `DATA`. Existing function, code, Host import,
export, and data records retain their v1 layout.

## Ku source contract

Ku selects a runtime module dependency with an explicit exact-version URI:

```ku
import "ku:std/type@1.0.0" as type

thought inspect(value) { type_is_list(value) }
```

The frontend lowers `type_is_list(value)` to a `MODULE_IMPORT` for identity
`ku:std/type`, version `1.0.0`, export symbol `is_list`, arity 1, followed by
`CALL_MODULE`. Imports are added lazily from actual calls and identical imports are
deduplicated. A conflicting arity for the same identity/version/export is rejected.

The URI is a frontend convention: it must contain a non-empty `ku:` identity and exactly
three unsigned decimal version components. The kernel continues to treat the resulting
identity as opaque UTF-8. Relative imports such as `import "type" as type` keep their Ku v1
source-composition meaning and use the configured source resolver.

The exact identities, dependency exports, and public arities of the migrated standard
library are frozen in [STDLIB_ABI.md](STDLIB_ABI.md).

## Loading and linking

`dao_vm_load_module` verifies and caches bytes without requiring dependencies to be present.
`dao_vm_link_module` registers an identified module in one VM and retains it until VM
destruction. Missing dependencies are allowed so independently compiled modules can be
loaded in any order. A call to a missing dependency returns `DAO_IMPORT_NOT_FOUND`.

Whenever a dependency is present, linking validates its exported symbol and parameter count.
It rejects identity conflicts, signature mismatches, and cycles among the currently linked
module graph. Re-linking the exact same bytes is idempotent. `dao_vm_find_module` returns a
retained reference; callers release it with `dao_module_release`.

The registry is VM-local and externally synchronized. It contains no process address,
filename, resolver callback, or global mutable registry.

## Call boundary

`CALL_MODULE` executes the target export inside the same VM call generation. It shares the
top-level instruction budget, call-depth limit, exception path, and List/Map arena.

Scalars, borrowed Bytes/String views, and current-generation Lists/Maps may cross the module
boundary. Function and Closure values are module-local and are rejected, including when
nested in a container. This prevents a function index from being reinterpreted against the
wrong module.

## Versioning rules

- A compatible implementation of an existing public API keeps its identity and exact
  version and must preserve exported symbol signatures.
- Any public signature change requires a new version.
- Different exact versions may be linked simultaneously.
- Module format or opcode changes require a new format/VM ABI pair.

## Non-goals

This ABI does not define package download, version ranges, filesystem resolution, module
globals, hot replacement, or cross-VM values. Host capability modules remain numeric
`IMPORT`/`CALL_HOST` dependencies documented in `FFI.md`.
