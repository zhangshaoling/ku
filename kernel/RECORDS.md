# Record kernel, source and tooling slice

Status: opt-in C++ builder / VM / C ABI implementation for v3/ABI11, with matching
recovery and self-hosted Ku source frontends and ABI11 assembly/disassembly libraries.
ModuleStore and Python support explicit opt-in; Python and Rust expose read-only
Record queries. See [binding usage](../docs/KU_RECORD_BINDINGS.md). This does not add Schema or Result/Option.

## Ku source

```ku
型 点 { 横: 整数, 纵: 整数 }
思 测试_记录() {
  值 = 点 { 纵: 2, 横: 40 }
  返 值.横 + 值.纵 == 42
}
```

Run `dao-ku --test file.ku`, or add `--recovery` to use the C++ frontend. Both
frontends emit identical bytes. Declarations may follow their uses; empty Records
and source-module namespace rewriting are supported. Field types are `整数/i64`,
`三值/Trit`, `字节/Bytes`, and `文本/String`. Bytes require an existing runtime value;
this slice adds no byte literal or Host Record constructor.

All fields are required exactly once. Initializers run in source order before
values are packed in declaration order. Known literal type mismatches, unknown
fields on a known Record, type-name shadowing, indexing and writes on a known
Record are compile errors. A local assigned exactly once to a direct constructor
can use a checked slot read; parameters, reassigned variables and container reads
use runtime field checks. This is conservative local inference, not Typed IR.

Record modules use `FIELD_GET` for dynamic dot reads, including ordinary Maps.
Record-free programs retain their existing dot-access lowering and module versions.
`dao-ku` enables Records only for verification and native tests of generated
v3/ABI11 modules. Other embedding VMs still require explicit opt-in.

The installed [record_tests.ku](../demos/record_tests.ku) demonstrates configuration
error Records and explicit conversion to the existing Map shape. The actual
configuration validator now uses the same pattern internally while preserving its
public Map result; see [the example](../docs/KU_CONFIG_EXAMPLE.md). This adds no Schema/Result operations.

## Creating and inspecting a value

The new value is `DAO_VALUE_RECORD` (9), with zero reserved bits and an opaque ID.
Record fields are fixed and read-only. Supported field types are I64, Trit, Bytes,
and UTF-8 String; no implicit conversions or nested Record fields are accepted.

```cpp
dao::ModuleBuilder builder;
const auto type = builder.add_record_type("Point");
builder.add_record_field(type, "x", DAO_VALUE_I64);
dao::FunctionSpec function;
function.register_count = 2;
function.code = {
    {dao::Opcode::LoadI64, 0, 0, 0, 0, 42},
    {dao::Opcode::MakeRecord, 0, 1, 0, 1, type},
    {dao::Opcode::Return, 0, 0, 1, 0, 0},
};
builder.add_function(function);
const auto bytes = builder.encode();
```

Create a VM and call `dao_vm_set_features(vm, DAO_FEATURE_RECORDS)` before loading
the bytes. The normal `dao_vm_load_module` and `dao_vm_call` then execute function
index 0. Inspect its result with `dao_value_record_get(vm, &result, 0, &field)`.
Use the installed CMake `dao_module_builder` target so its runtime dependency is linked.

`dao_module_record_count/name/field_count/field_info` inspect type descriptors;
`dao_value_record_type` returns a retained owning module and a type index. Release
that module with `dao_module_release`. The complete signatures and byte layouts are
in the [R0 protocol](../docs/KU_RECORD_R0_ABI.md).

## Capability and lifetime rules

- VMs start with features 0. v3 requires `DAO_FEATURE_RECORDS`; otherwise load,
  link or call returns `DAO_UNSUPPORTED_FEATURE` (17), before execution.
- Successful load/link or an accepted top-level call locks the mask. Setting the
  same mask again is allowed; changing it or setting unknown bits is rejected.
- Descriptor queries do not require a VM. Value queries require the owning VM
  with the feature enabled. Failures clear valid output parameters.
- A Record belongs to the current call generation. Top-level input of a Record is
  rejected before advancing the generation. A later accepted call invalidates it.
- Arena-held module references preserve descriptors through caller release and
  cache eviction. String/Bytes fields still borrow their underlying storage.
- IDs are never reused by the loaded kernel instance and are looked up in a
  VM-local table. A different VM cannot resolve another VM's Record ID.
- List/Map values may contain Records internally. Host/module boundaries reject
  Records recursively, including module exception payloads. No Host constructor
  or setter is provided. Boundary traversal handles cycles and shares the VM budget.

Ordinary Map access, old opcode meanings and v1/ABI9 / v2/ABI10 encoding remain
unchanged. Generic embedding VMs keep features 0. `dao-ku` enables its internal
verification/test VMs for v3 source, and `ku-config` explicitly enables Records for
its packaged validator. Other callers choose their own feature policy.
Assembly/disassembly preserve v3 versions and Record descriptors using the
[ABI11 text extension](FORMAT.md#abi11-assembly-text). They validate v3 with a temporary
Record-enabled VM. Numeric AOT continues to reject all v3 inputs. ModuleStore rejects
them by default; `ModuleStore(root, DAO_FEATURE_RECORDS)` and the explicit CLI
`--enable-records` switch permit verified identified modules. The same policy checks
derived parents, without changing manifest schema 1. See [Store feature policy](MODULE_ABI.md#store-feature-policy).

```powershell
dao-ku --enable-records --store ./store --identity ku:example/record 1.0.0 ./record.ku
dao-module --enable-records run ./store ku:example/record 1.0.0 main
```

The example assumes `main` returns a scalar. Inspect Record/container results through
the C ABI or Python Runtime query methods. CLI structured output and Record value
serialization are not part of this slice.

## Verification

```powershell
. .\tools\activate_toolchain.ps1
cmake --build kernel/out/cmake --parallel 2
ctest --test-dir kernel/out/cmake --output-on-failure -R '^record_'
ctest --test-dir kernel/out/cmake --output-on-failure --parallel 2 -E aot_performance_gate
```

`record_wire_contract` runs the 14 reviewed R0 byte cases against the production C ABI,
including runtime field-type errors. `record_conformance` checks builder byte identity,
all scalar field types, static/dynamic reads, malformed operands/descriptors, capability
locking, foreign/stale IDs, module retention, Map behavior, cyclic containers, boundary
budget and rejection before Host/callee side effects. The old ABI/compiler suite remains required.

Windows local verification on 2026-11-22: full functional CTest 66/66 passed; additional
Record cases passed on retest; isolated ASan/UBSan Record conformance passed.
The extracted Windows SDK passed the same 14 wire cases with development PATH entries
removed, as well as the existing SDK configuration/diagnostic/memory/binding checks.
These kernel-slice results do not establish Linux/macOS portability. Source frontend
validation is recorded separately below.

Windows source frontend validation on 2026-11-23: functional CTest 69/69 passed
(excluding the separate AOT performance gate). `record_frontends` covers exact
dual-frontend byte identity, source imports, initializer order, chained reads,
reassignment and binding inference, formatting, original-source diagnostics and
26 invalid inputs. `ku_selfhost_seed` also executes Record cases with the rebuilt
compiler and requires B0 == B1 == B2. Its compiler rebuild budget is 200M
instructions after the former 100M budget was exhausted; production compile
budgets are unchanged. The extracted Windows SDK passed its installed Record example through both frontends
with development PATH entries removed, including an exact byte comparison. Existing
configuration, diagnostics, code-memory and binding installation checks also passed.

The ABI11 tooling slice adds `record_tooling` for canonical binary/text round trips,
all three Record opcodes, multiple/empty types, scalar tags, identity/module imports,
shared and unused DATA names, malformed descriptors/operands and every truncation of
a valid module. Legacy v1/v2 byte identity remains checked. `record_aot_rejection`
checks unused declarations and preserves absent/existing output files. The assembly,
disassembly and Record tooling tests also pass in the isolated ASan/UBSan build.

`record_store` covers opt-in/default policy isolation, source and derived saves,
parent policy, unchanged VM cache/link checks, scalar/Record module boundaries and
read-only inspection. `record_store_cli` additionally checks dual-frontend byte identity,
schema 1, unused declarations, tampered bytes/source and rejection of opaque outputs.

`python_record_binding` exercises the Python Runtime and CodeMemory policies,
all scalar fields, descriptors, stale/foreign/closed handles, owner retention,
container reads, unsupported integer inputs/results and Host boundary rejection.
`rust_record_binding` compiles a native Rust probe and calls the real C ABI to verify
field signatures, scalar values, output clearing, capability locking and lifetimes.


The R1 review and configuration migration are recorded in
[the acceptance matrix](../docs/KU_RECORD_R1_ACCEPTANCE.md). The migrated validator
uses four-field error Records internally and returns fresh Maps at the existing
public boundary. Source import tests additionally check independent sibling ancestry
and preservation of field names that match declarations. The previously recorded
`keys(Map)` selfhost/recovery difference is fixed with exact builtin-name matching;
`ku_keys_frontends` and both compiler generations check normal and similarly named calls.
