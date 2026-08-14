# Ku Host FFI (`dao_*` ABI)

Ku calls host capabilities through numeric imports and the stable `dao_*` C ABI. The runtime does not serialize calls through JSON, look up string names, or store process addresses in a module. The complete value-lifetime contract is in [`OWNERSHIP.md`](OWNERSHIP.md).

## Module Side

A module declares each host dependency with:

- a numeric symbol ID
- a fixed parameter count

`CALL_HOST` addresses the declaration by import index. Loading a module does not require the host function to be registered; an unresolved call returns `DAO_IMPORT_NOT_FOUND` with its function and instruction location.

## Host Side

Register a callback on a VM before calling code that uses it:

```c
static dao_status add(void* context, const dao_value* args, size_t count,
                      dao_value* out) {
    if (count != 2 || args[0].type != DAO_VALUE_I64 ||
        args[1].type != DAO_VALUE_I64) {
        return DAO_TYPE_ERROR;
    }
    const int64_t bias = *(const int64_t*)context;
    *out = (dao_value){DAO_VALUE_I64, 0, args[0].payload + args[1].payload + bias};
    return DAO_OK;
}

int64_t bias = 1;
dao_host_function function = {
    sizeof(dao_host_function), 700, 2, 0, add, &bias
};
dao_status status = dao_vm_register_host_function(vm, &function);
```

The callback receives a read-only contiguous argument view and writes one result. Dao validates the declared signature, callback status, result type, Trit range, and reserved fields.

## Borrowed Bytes And Strings

`DAO_VALUE_BYTES` and `DAO_VALUE_STRING` are zero-copy borrowed views. Construct and inspect them through the C ABI helpers:

```c
const uint8_t payload[] = {1, 2, 3};
dao_value value;
dao_status status = dao_value_make_bytes_view(
    (dao_bytes){payload, sizeof(payload)}, &value);

dao_bytes view;
status = dao_value_get_view(&value, &view);
```

`dao_value_make_string_view` accepts UTF-8 bytes and rejects malformed encodings. Both value types preserve the original pointer across register moves, internal calls, returns, and host callbacks. The 16-byte `dao_value` stores the byte length in `reserved` for view types, so one view is limited to 4 GiB. Scalar values still require `reserved == 0`.

These APIs create borrowed views only. Owned buffers require an allocator and ownership handle contract and are intentionally deferred instead of hiding allocation behind the C ABI.

## Lifetime And Concurrency

- `user_data` remains host-owned and must stay valid until the function is unregistered and all active calls have returned.
- Duplicate registration is rejected. Unregistering an absent symbol returns `DAO_IMPORT_NOT_FOUND`.
- A VM is externally synchronized. Registration, calls, container inspection, cache mutation, and destruction must not race on the same VM.
- Use one VM per concurrent worker or serialize the complete call-and-inspection sequence. Immutable verified modules may be shared independently of VM execution state.
- A callback must not throw across the C boundary. The C++ runtime catches an accidental exception and returns `DAO_RUNTIME_ERROR`, but bindings must not rely on exceptions for control flow.
- A callback must not retain the argument pointer or output pointer after returning.
- View storage remains owned by the caller or host. It must stay readable for the entire VM call and for as long as the returned view is consumed.
- A host must not return a view into callback stack storage. Returning static, arena, module, or explicitly host-owned storage is valid when its lifetime is documented.
- The VM does not mutate view bytes. Hosts must synchronize mutable backing storage if calls can run concurrently.

The registry belongs to the VM, not the module. The same verified module can therefore run against different host capability sets without changing its bytes or fingerprint.

## Identified Module Linking

Dao Binary Module v2 / VM ABI v10 modules expose an explicit UTF-8 logical identity and
exact semantic version. Query it with `dao_module_get_identity`. Load and verification stay
independent from linking: `dao_vm_load_module` accepts unresolved dependencies, while
`dao_vm_link_module` retains an identified module in the VM-local registry.

`dao_vm_find_module` returns a retained reference for an exact identity/version; release it
with `dao_module_release`. Re-linking equal bytes is idempotent. Different bytes under the
same identity/version, resolved signature mismatches, and linked cycles are rejected.
Legacy v1/ABI9 modules remain executable but return `DAO_MODULE_IDENTITY_MISSING` when linked.

`CALL_MODULE` is not Host FFI. It resolves a module-import record and shares the current VM
instruction budget, call depth, exception path, and container generation. Function and
Closure values may not cross module boundaries. See [`MODULE_ABI.md`](MODULE_ABI.md).

## Verified Module Cache

The verified-module cache defaults to 64 entries. Configure it through
`dao_vm_set_module_cache_capacity`; `dao_vm_config.reserved` remains zero for C ABI
compatibility. Cache identity is fingerprint plus full byte equality. Use
`dao_vm_get_cache_stats` for hit/miss counters and `dao_vm_clear_module_cache` to release
cache-owned references. Caller-owned module references remain valid after a clear.

## VM-Owned Container Handles

List and map payloads are opaque generation handles, not process pointers. They remain
valid until the next top-level call on the same VM. Hosts can inspect returned containers
with `dao_value_list_size`, `dao_value_list_get`, and `dao_value_map_get`. Stale handles
return `DAO_RUNTIME_ERROR`.

During a registered Host callback, the Host may construct VM-owned results with
`dao_vm_make_list`, `dao_value_list_append`, `dao_vm_make_map`, and `dao_value_map_set`.
Construction outside a callback is rejected, inserted container values must belong to the
same VM generation, and foreign handles returned by callbacks are rejected. These APIs are
stable parts of the current C ABI. Function and Closure values cannot be manufactured by a
Host, including inside a container. Callbacks that construct containers receive the owning
`dao_vm*` through their `user_data` context.
