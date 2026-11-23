# 原生测试命令

`dao-ku --test` 是 v1.2.0 开发中的原生测试入口。当前已发布版本见
[Releases](https://github.com/zhangshaoling/ku/releases)。

## 编写并运行

在 UTF-8 `.ku` 文件中定义无参数测试函数，名称以 `测试_` 或 `test_` 开头。
返回 `真` 表示通过，返回其他值或抛出异常表示失败。

```ku
思 加一(x) { 返 x + 1 }

思 测试_加一() {
  若 加一(41) != 42 { 抛 "加一结果应为 42" }
  返 真
}
```

在仓库根目录完成构建后运行以下命令。

```powershell
.\kernel\out\cmake\bin\dao-ku.exe --test demos/native_tests.ku
.\kernel\out\cmake\bin\dao-ku.exe --test --recovery demos/native_tests.ku
```

Linux/macOS 使用 `./kernel/out/cmake/bin/dao-ku`。SDK 安装后的工具位于 `bin`。
示例输出如下。

```text
[PASS] 测试_列表求和
[PASS] 测试_映射读取
[PASS] 测试_异常捕获
tests: 3 passed, 0 failed, 3 total
```

## 发现与执行规则

- 每次接收一个源码文件，只发现入口文件中以约定前缀命名的顶层函数，并按声明顺序执行。
- 注释、字符串、辅助函数和导入模块中的测试声明不自动执行。源码导入使用现有模块根和循环校验规则。
- 默认通过自举前端编译，`--recovery` 显式选择 C++ 恢复前端。函数名发现复用现有源语言解析器。
- 每个测试使用独立 VM 和默认执行预算。预算耗尽、运行时错误、参数不匹配与未注册的 Host 调用均记为失败，继续执行后续测试。
- 只有 Trit `+1`（`真`）通过。整数 `1`、`空`、Trit `0` 和 `假` 均失败。
- 失败行包含函数名、运行时状态及可用的异常消息。每个文件结束后输出通过、失败和总数。

测试执行期间使用新内核默认配置。外部 Host 能力与二进制模块依赖需要完整的宿主环境，
当前测试入口提供纯语言及源码组合模块的执行路径，并注册 `source_len/1`、
`source_byte/2`、`source_slice/3` 三个只读文本能力。长度与下标按 UTF-8 字节计算，
切片须保持合法 UTF-8 边界；不注册文件或网络能力。实际用例见 [配置校验器](KU_CONFIG_EXAMPLE.md)。

## 退出码

| 退出码 | 含义 |
|---|---|
| `0` | 至少发现一个测试，全部通过 |
| `1` | 至少一个测试失败 |
| `2` | 用法、读取、编译、验证或发现错误，或没有发现测试 |

`--test` 接收一个输入路径，与 `--check`、`--fmt`、`--store`、`--identity`、`--parent` 及输出路径互斥。
测试运行不生成 `.dao` 文件或存储记录。

## Record 源码前端

```powershell
ctest --test-dir kernel/out/cmake --output-on-failure --parallel 2 -R '^(record_|ku_selfhost_)'
kernel/out/cmake/bin/dao-ku --test demos/record_tests.ku
```

`record_frontends` 校验双前端字节一致、导入和错误定位、求值顺序、重赋值/绑定、
链式读取、格式化和非法输入；`ku_selfhost_seed` 继续要求 B0=B1=B2，并验证重建
编译器生成的 Record。安装包验证同时运行包内 Record 示例与两个前端的字节比较。
完整功能回归仍排除独立的 `aot_performance_gate`，不据此宣称性能门禁已运行。

## ABI11 汇编工具

`record_tooling` 覆盖规范二进制/文本往返、三条 Record 指令、空类型、模块身份/导入、
四种标量字段、共享字符串索引、非法描述与操作数和截断输入；旧 v1/v2 继续逐字节比较。
`record_aot_rejection` 检查数值 AOT 拒绝只有类型声明的 v3，并保护已有输出文件。

```powershell
ctest --test-dir kernel/out/cmake --output-on-failure --parallel 2 -R '^(record_tooling|record_aot_rejection|assemble|disassemble)$'
```

安装后，`kernel/tests/sdk_consumer` 的 C++ consumer 只链接 SDK 导出的
`Dao::dao_assembly`、`Dao::dao_disassembly` 和 `Dao::dao_module_store`，验证传递依赖、Record 字节往返与 Store 能力策略。
`tools/verify_installed_sdk.py` 同时验证包内 AOT 对 ABI11 的拒绝与输出保护。

## Record ModuleStore

```powershell
ctest --test-dir kernel/out/cmake --output-on-failure --parallel 2 -R '^(record_store|record_store_cli|module_store|module_store_contract)$'
```

`record_store` 校验默认拒绝、显式开启、独立 Store/VM/cache 策略、源码/派生保存、父记录
策略和跨模块值限制。CLI 的 7 组用例覆盖双前端相同存储字节、schema 1、未使用的 Record
声明、来源/字节篡改、派生父校验以及不把 Record/容器句柄打印为整数。
独立 SDK consumer 与隔离 PATH 的包测试同时覆盖显式开启和默认拒绝。

## Python / Rust Record 绑定

`python_record_binding` 检查默认关闭、策略锁定、四种标量、描述查询、generation/外来句柄、
owner 保活与关闭、容器读取、整数入口拒绝、Host 边界和 CodeMemory 策略。
`rust_record_binding` 用 rustc 编译 no_std 原生探针并实际调用内核，验证 FFI 签名和生命周期。
Windows 探针从公开头文件生成临时导入库，并使用 Rust 自带链接器，避免依赖 Visual C++ CRT。
Rust 编译器可用（Windows 还需 llvm-dlltool）时加入此门禁；`rust_binding_check` 仍保留。
SDK 检查使用包内 Python/Rust 绑定和动态库；有 Rust 工具链时也运行包内探针。
用法与生命周期契约见 [KU_RECORD_BINDINGS.md](KU_RECORD_BINDINGS.md)。

## R1 核对与配置迁移

逐项证据见 [R1 验收记录](KU_RECORD_R1_ACCEPTANCE.md)。配置校验器现用 Record 保存内部
错误，在公开返回前导出原 Map；12 个语言测试和 CLI 完整文本检查约束兼容性。
`record_frontends` 的 18 组用例包含两个别名、共享依赖、与声明同名的字段及重复字段定位。
对应导入源码也由 `ku_selfhost_seed` 的初始和重建编译器运行。

## keys(Map) 双前端兼容

`ku_keys_frontends` 包含 5 组用例，检查空 Map、中文排序、只求值一次、相似函数名、
错误参数数量、非法输入和 Record 拒绝，同时比较合法源码的字节产物。
`ku_selfhost_seed` 在初始与重建编译器中执行对应正反例；生产 acceptance 和 SDK 测试
保留覆盖。可用 `ctest --test-dir kernel/out/cmake --output-on-failure -R '^ku_keys_frontends$'` 定向运行。
