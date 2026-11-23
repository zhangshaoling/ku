# Record 的 Python / Rust 只读绑定

当前开发切片提供显式能力开关、模块类型描述和 Record 字段查询。默认 VM/CodeMemory
能力仍为 0；没有新增 Python Record 构造器、隐式 Map 转换、跨调用句柄传参或运行值序列化。

## Python Runtime

先使用 `dao-ku` 编译含 `型` 的源码。例如 `record.ku`：

```ku
型 点 { 横: 整数, 名称: 文本 }
思 main() { 返 点 { 横: 42, 名称: "中文" } }
```

```python
from pathlib import Path
from dao_kernel import Runtime, DAO_FEATURE_RECORDS, fnv1a

with Runtime("path/to/libdao_kernel.dll", features=DAO_FEATURE_RECORDS) as runtime:
    with runtime.load(Path("record.dao").read_bytes()) as module:
        assert module.required_features == DAO_FEATURE_RECORDS
        assert module.record_count == 1
        assert module.record_name(0) == "点"
        assert module.record_field_count(0) == 2
        assert module.record_field_info(0, 0) == ("横", 1)
        record = module.call(fnv1a("main"))
        owner, type_index = runtime.record_type(record)
        with owner:
            assert owner.record_name(type_index) == "点"
            assert runtime.record_get(record, 0).payload == 42
            text = runtime.value_text(runtime.record_get(record, 1))
            assert text == "中文"
```

`runtime.features` 查询当前掩码；`set_features(mask)` 只允许在首次成功加载、链接或接受
顶层调用之前改变。锁定后重复设置同值允许，修改掩码失败。未知能力位和超范围索引
由 Python 明确拒绝；其余无效状态以 `DaoKernelError` 报告。

`record_type` 返回 `(Module, type_index)`。该 Module 持有独立的 retained 引用，必须
使用上下文管理器或 `close()` 释放。类型身份由拥有模块和本地类型索引共同组成；不能仅凭
类型名称或字段列表认定两个类型相同。类型描述在拥有模块释放之前有效，即使 Runtime 已关闭。

`record_get` 返回原始 `DaoValue`，不改变字段绑定。Record 本身只在所属 VM 当前 generation
内可用；下一次接受的顶层调用会使旧句柄失效，另一 VM 无法使用它。引用计数只保留模块
描述，不延长 Record 句柄寿命。关闭原始 Module 或清理缓存时，arena 仍保留当前 Record 的 owner。

String/Bytes 字段仍遵循 C ABI 借用规则。在 owner、调用参数和当前值仍有效时，用
`value_text` 或 `value_bytes` 复制为 Python 自有对象；返回的原始字段值不自动携带 owner。
不要在后续调用或释放原借用存储后读取旧视图。标量字段标签为 I64=1、Trit=2、Bytes=3、String=4。

List/Map 中的 Record 可分别经 `list_values` / `map_value` 取出，再使用相同查询。
`Module.call` 继续仅接受 `int/str` 参数，拒绝直接重用 Record/容器句柄；`call_i64` 和
`Thought.call_i64` 不接受 Record 返回值。`Thought.call` 可在显式启用的 Runtime 中返回
原始 Record，随后按上述方式查询。Host/模块边界仍由内核递归拒绝 Record。

## Python CodeMemory

```python
from dao_kernel import CodeMemory, DAO_FEATURE_RECORDS

memory = CodeMemory("./store", features=DAO_FEATURE_RECORDS)
memory.save_source(
    "型 点 { 横: 整数 } 思 main() { 值 = 点 { 横: 42 }; 返 值.横 }",
    "ku:agent/point",
)
assert memory.run("ku:agent/point") == 42
```

同一实例的保存、派生保存、验证与执行都会传递原生 `--enable-records` 开关。
默认实例不能因此读取 Record 模块。manifest 保持 schema 1，`run` 仍返回整数；要查询
Record，使用显式启用的 Runtime 和 C ABI 查询路径。这里持久化的是代码模块，不是运行时句柄。

## Rust 系统绑定

`dao-kernel-sys` 是 `no_std` 的原始 C ABI 声明，不提供安全封装或隐式生命周期管理。
已暴露 `DAO_FEATURE_RECORDS`、`DAO_UNSUPPORTED_FEATURE`、`DAO_VALUE_RECORD` 和下列函数：

- `dao_vm_set_features` / `dao_vm_get_features`；
- `dao_module_required_features`；
- `dao_module_record_count/name/field_count/field_info`；
- `dao_value_record_type` / `dao_value_record_get`；
- `dao_value_get_view` / `dao_value_make_bytes_view`。

创建 VM 后，在加载前调用 `dao_vm_set_features(vm, DAO_FEATURE_RECORDS)`，并检查每个
返回状态。`dao_value_record_type` 的 `out_owner` 是 retained 指针，调用方必须用
`dao_module_release` 释放；字段视图仍借用原存储。原始指针调用都需要 `unsafe`，其他
generation、边界和能力规则与 Python/C ABI 相同。

`build.rs` 沿用 `DAO_KERNEL_LIB_DIR` 指定原生链接库目录。原生集成探针
`bindings/rust/dao-kernel-sys/record_probe.rs` 通过实际内核调用验证 ABI，使用驱动
`bindings/rust/test_records.py` 编译并运行，不只检查声明是否能通过 `cargo check`。

## 验证

```powershell
ctest --test-dir kernel/out/cmake --output-on-failure --parallel 2 -R '^(python_record_binding|rust_record_binding|python_binding|python_code_memory|python_memory_host|rust_binding_check)$'
```

Python 测试覆盖四种标量、空 Record、容器读取、默认策略、能力锁定、失效/外来句柄、
owner 保活与关闭、旧整数入口拒绝、Host 边界及 CodeMemory。Rust 探针覆盖真实函数签名、
四种标量、输出清零、保活、generation 和能力规则。SDK 验证使用解压包内绑定与动态库；
环境存在 Rust 工具链时，也运行包内原生 Rust 探针。本轮实测平台为 Windows。
