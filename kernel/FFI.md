# Dao Host FFI

Dao VM ABI 2 calls host capabilities through numeric imports and the stable C ABI. The runtime does not serialize calls through JSON, look up string names, or store process addresses in a module.

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
- Registration and unregistration must not race with calls on the same VM.
- After registration is frozen, immutable modules and a VM may be used for concurrent calls; callback code and `user_data` must provide their own thread safety.
- A callback must not throw across the C boundary. The C++ runtime catches an accidental exception and returns `DAO_RUNTIME_ERROR`, but bindings must not rely on exceptions for control flow.
- A callback must not retain the argument pointer or output pointer after returning.
- View storage remains owned by the caller or host. It must stay readable for the entire VM call and for as long as the returned view is consumed.
- A host must not return a view into callback stack storage. Returning static, arena, module, or explicitly host-owned storage is valid when its lifetime is documented.
- The VM does not mutate view bytes. Hosts must synchronize mutable backing storage if calls can run concurrently.

The registry belongs to the VM, not the module. The same verified module can therefore run against different host capability sets without changing its bytes or fingerprint.
