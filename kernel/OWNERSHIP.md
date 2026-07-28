# Ku C ABI Value Ownership

> Status: frozen for the current C ABI

This document defines the ownership and lifetime of every `dao_value` category. The
`dao_*` prefix is the current versioned implementation ABI for the Ku kernel.

## Value classes

| Value | Owner | Lifetime | May be a top-level argument? |
|---|---|---|---|
| `NULL`, `I64`, `TRIT` | value itself | copied by value | yes |
| `BYTES`, `STRING` | caller, Host, or immutable module | borrowed storage must outlive every consumer | yes |
| `LIST`, `MAP` | one VM call-generation arena | until the next accepted top-level call on that VM | no |
| `FUNCTION` | the executing module | only meaningful inside that module execution | no |
| `CLOSURE` | one VM call-generation arena | until the next accepted top-level call on that VM | no |

`dao_value` never owns a heap allocation by itself. Copying a value copies scalar bits, a
borrowed pointer, a module-local function index, or an opaque generation handle; it does not
extend the underlying lifetime.

## Borrowed bytes and strings

`dao_value_make_bytes_view` and `dao_value_make_string_view` store the original pointer and
do not copy bytes. The storage owner must keep it readable for as long as any direct or
nested view is inspected.

- Caller input views must remain valid for the complete `dao_vm_call`.
- A returned caller view remains caller-owned.
- A module string view remains valid while its `dao_module` is retained.
- A Host-returned view must use static, arena, module, or explicitly Host-owned storage; it
  must never point into callback stack storage.
- A view inserted into a List or Map remains borrowed. The container does not copy its bytes.
- Map keys are different: `dao_value_map_set` validates UTF-8 and copies the key into the VM.

The current ABI deliberately has no hidden owned-string allocator. A future owned buffer
type requires an explicit allocation and release contract.

## VM generation handles

List, Map, and Closure payloads encode a VM generation and an arena index. They are not
process pointers.

At the start of every accepted `dao_vm_call`, after basic argument validation, the VM clears
the previous call arena and advances the generation. This invalidates all List, Map, and
Closure values returned by the previous call even when the new call later returns an error.
Calls rejected during basic argument validation do not advance the generation.

Container inspection requires the same owning VM:

- valid current-generation handle: operation proceeds;
- stale or foreign handle: `DAO_RUNTIME_ERROR`;
- wrong value type or malformed API argument: `DAO_INVALID_ARGUMENT`.

The ABI has no retain operation for generation values. Code that needs a container after the
next call must copy it into Host-owned storage or persist it through an explicit Host module.

## Host-created containers

The following APIs are stable parts of the current C ABI:

- `dao_vm_make_list`
- `dao_value_list_append`
- `dao_vm_make_map`
- `dao_value_map_set`

They are valid only while a registered Host callback is executing on the owning VM. Because
the callback signature keeps the ABI small, a callback that constructs containers stores the
owning `dao_vm*` in its `user_data` context.

Host insertion accepts:

- `NULL`, `I64`, and `TRIT`;
- borrowed `BYTES` and `STRING` views with valid backing storage;
- current-generation List and Map handles from the same VM.

It rejects stale or foreign handles and all Function or Closure values. Hosts cannot
manufacture callable values, including by hiding one inside a container.

## Concurrency and destruction

A `dao_vm` is externally synchronized. The Host must not race calls, container inspection,
Host registration, cache mutation, or destruction on the same VM. Use one VM per concurrent
worker or serialize access around the complete call-and-inspection sequence.

Verified `dao_module` objects are immutable and reference-counted. A caller-owned module
reference survives cache clearing and may be shared when each consuming VM follows its own
synchronization rule. `dao_vm_destroy` releases all VM-owned generation arenas and cache
references; `dao_module_release` releases a caller-owned module reference.

`dao_vm_link_module` adds a separate VM-owned reference that survives release of the caller's
handle and is released by `dao_vm_destroy`. `dao_vm_find_module` returns a new caller-owned
reference. Module calls share the same generation arena, but Function and Closure values are
module-local and cannot cross that boundary, including inside Lists or Maps.

## Non-goals

This contract does not introduce a process-wide garbage collector, reference-counted
containers, cross-call closures, or portable-C AOT containers. Those require separate ABI
projects and must not silently weaken this lifetime model.
