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

`dao::store::ModuleStore` is the optional host-side persistence layer for identified
modules. It stores verified `.dao` bytes under an identity-hash/version path and may keep
an adjacent `.ku` source file for provenance. Loading from the store validates the module
through the normal loader and requires the embedded identity/version to match the lookup
key. The store does not alter linking: callers still load, link, and execute through the
VM APIs. `ModuleStore::save_source` is the minimal code-memory entry point: it compiles
source through the self-hosted frontend with an explicit identity/version, then persists
the verified result and source provenance in one store record. `save_derived_source`
adds a parent identity/version to the manifest, allowing code memories to form an
explicit, auditable version chain without changing the binary ABI.

### Store feature policy

`ModuleStore(root, features = 0)` applies an explicit feature mask to every internal
validation VM. The default rejects v3/ABI11 with `DAO_UNSUPPORTED_FEATURE`, even if
its Record types are unused. `ModuleStore(root, DAO_FEATURE_RECORDS)` permits verified
identified Record modules. Unknown mask bits throw `std::invalid_argument` during
construction, before filesystem operations. The mask has no setter or global effect.

The policy covers `save`, `load`, `verify`, `save_source` and `save_derived_source`,
including validation of a derived record's parent. Enabling it does not skip verifier,
identity, checksum, immutability or provenance checks. Failed reads still clear the
output vector. Each load validates afresh; another Store instance's enabled reads and
an execution VM's module cache cannot authorize a default Store. A rejected Record
save creates no store; a rejected derivation publishes no child bytes or manifest.

The mask is caller policy, not new persisted metadata: manifests remain schema 1.
Loading a legacy child does not recursively read its recorded parents or runtime
dependencies. Callers must separately load dependencies under their policy, then
enable the target VM before loading/linking/execution. Scalars may cross module
boundaries; Record values, including nested values, remain rejected there.

```powershell
dao-ku --enable-records --store ./store --identity ku:app/record 1.0.0 ./record.ku
dao-module --enable-records verify ./store ku:app/record 1.0.0
dao-module --enable-records run ./store ku:app/record 1.0.0 main
```

`dao-ku --enable-records` requires `--store`; it does not change ordinary compile,
check or test usage. `dao-module` accepts the flag before its command and applies the
same mask to Store verification and its execution VM. With the flag, `run`/`recall`
reject Record/List/Map results instead of printing their opaque handles as integers;
use the C ABI query APIs to inspect them. No value serialization or automatic dependency
linking is added. High-level binding integration remains a separate task.

### Store manifest schema 1

The UTF-8 JSON manifest is a flat object. Field order and JSON whitespace are insignificant,
and strings use standard JSON escaping, including Unicode escapes.

| Field | Contract |
|---|---|
| `schema_version` | Required integer `1` |
| `identity` | Required embedded module identity |
| `version` | Required exact `major.minor.patch` module version |
| `fingerprint` | Required hexadecimal FNV-1a checksum using the existing store seed |
| `source_fingerprint` | Optional hexadecimal checksum of the exact adjacent `.ku` bytes |
| `parent_identity`, `parent_version` | Optional pair describing one parent record |

Unknown fields, unsupported schema versions, incomplete parent pairs, malformed JSON,
and duplicate fields are rejected. For compatibility with early schema-1 writers,
identical repeated parent fields remain readable. Conflicting repeated parent fields are
rejected. Legacy manifests without `source_fingerprint` remain readable with their original
guarantees. Newly saved source provenance includes its checksum. Checksums establish
file consistency. They do not authenticate the author or prove that caller-supplied source
produces the supplied module bytes.

### Store immutability and derivation

At a committed identity/version, different module bytes, source provenance, or an explicitly
different parent return `DAO_MODULE_CONFLICT`. Repeating the same derived save preserves
the existing manifest bytes. An ordinary save of the same module preserves its recorded
parent. A base record cannot acquire a parent after it has committed.

`save_derived_source` verifies the exact parent in the same store before publishing a new
child. A missing parent returns `DAO_IMPORT_NOT_FOUND`, and a self-parent returns
`DAO_MODULE_CYCLE`. Parent records precede children and committed provenance is immutable.
Reads validate the requested record without recursively loading its ancestors.

### Store publication and recovery

Module bytes and optional source are written through temporary files with checked stream
completion. The complete manifest, including any parent, is published last by rename.
`load` returns bytes only after module identity/version, manifest, and any declared source
checksum pass validation. Every failed load clears the output vector.

After an interrupted write, retrying with the same bytes, source, and parent completes a
missing manifest. A committed record with missing source can be restored by retrying with
source matching its recorded checksum. Corrupt manifests or existing mismatched files are
rejected with `DAO_BAD_MODULE` or `DAO_MODULE_CONFLICT`. I/O failures return
`DAO_RUNTIME_ERROR` and leave committed records intact. Temporary files may remain after
failure and are reused by a subsequent write. This contract covers retry recovery after
process interruption and reported I/O failure. Power-loss durability is not guaranteed.

### Store concurrency

Native saves use one nonblocking OS lock on `<store>/.write.lock`, covering all store objects,
threads, and cooperating processes for that directory. An overlapping writer returns
`DAO_RUNTIME_ERROR` with a busy diagnostic. Lock handles close on return or process exit,
and the lock file remains in place. The supported environment is a local filesystem with
OS locking and atomic rename semantics.

Readers can access committed records during a write. A new record whose manifest has not
yet published can return not-found or validation failure until the writer commits.
Python CodeMemory creates a separate temporary source for each call, preserving input
isolation before the native store lock is acquired.

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
