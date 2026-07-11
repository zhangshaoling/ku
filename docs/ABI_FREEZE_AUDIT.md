# Dao ABI Freeze Audit

Authority: `docs/DAO_KERNEL_IMPLEMENTATION_GUIDE.md`.

## Frozen And Compatible

- Dao Binary Module header and section-table encoding.
- Numeric import/export records and host callback ABI.
- `dao_value` size and scalar/view field layout.
- Existing `dao_vm_config` layout, including the zero `reserved` field.
- Loader/verifier, instruction budgets, module reference ownership, and error records.

Cache capacity is configured through `dao_vm_set_module_cache_capacity`; it does not
reuse or reinterpret a reserved C ABI field.

## Experimental, Not Yet Frozen

- `DAO_VALUE_LIST` and `DAO_VALUE_MAP` ownership and cross-call behavior.
- Container construction and mutation from host bindings.
- The VM call-arena strategy and future generation-handle representation.
- VM ABI v7 container and exception opcodes.
- The portable-C AOT subset and its exported native function convention.

These interfaces must not be advertised as long-term stable until representation
benchmarks, legacy parity tests, and host container APIs are complete.

## Required Before K6

1. Benchmark tagged-union, compact-handle, and arena container representations.
2. Define owned container handles and host inspection/construction APIs.
3. Run the legacy `.ku` parity corpus, including old import syntax and mutation behavior.
4. Expand AOT to FFI, containers, and exceptions where a native path is justified.
5. Declare a stable C ABI version and struct-extension compatibility policy.
