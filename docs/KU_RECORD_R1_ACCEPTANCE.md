# R1 同模块标量 Record 验收记录

日期：2026-11-23。依据为本地 Windows 工作区执行结果；未发布新版本。

本轮核对 [RFC 验收清单](KU_RECORD_SCHEMA_RESULT_RFC.md#10-验收清单) 中的
REC-01 至 REC-13 和 REG-01，并完成配置错误项的 Record 内部迁移。表中“通过”仅表示
所列用例在当前 Windows 构建上通过，不代表 Linux/macOS、性能门禁或整个语言规范已验证。

## 验收证据

| 项目 | 当前证据 | 结果 |
|---|---|---|
| REC-01 中文声明与具名构造 | `record_frontends` 的中文、前向声明、标量与空类型用例；双前端逐字节比较 | 通过 |
| REC-02 重复字段 | 非法声明/构造用例；新增两个前端均指向重复字段的 `:3:3` 位置检查 | 通过 |
| REC-03 缺失/多余字段 | 两个前端的缺失、多余、重复构造项拒绝用例 | 通过 |
| REC-04 严格标量类型 | 静态 Trit/文本构造拒绝；动态类型错误；C ABI 与 Python/Rust 四种标量读取 | 通过 |
| REC-05 求值顺序 | `test_initializer_evaluation_order` 记录两个副作用的次序，并检查字段槽位结果 | 通过 |
| REC-06 不支持的 Record 操作 | 静态未知字段/写入/索引拒绝；动态未知字段、写入、索引、相等和长度产生类型错误；普通 Map 缺失字段仍为空 | 通过 |
| REC-07 名义类型身份 | `record_conformance::instruction_contracts` 对相同字段的两个声明使用错误类型索引读取，得到类型错误；Record 相等不开放 | 通过 |
| REC-08 容器内读取 | 双前端 List/Map 链式读取、Python 容器读取及配置内部错误列表 | 通过 |
| REC-09 生命周期 | 原生/Python/Rust 的 stale/foreign、owner 保活、缓存清理与模块释放用例 | 通过 |
| REC-10 Host/模块边界 | 原生含循环容器、预算、异常载荷的边界用例；Python callback 调用前拒绝嵌套 Record | 通过 |
| REC-11 协议与描述反例 | 14 个 R0 wire 样本、原生 verifier 反例、汇编非法操作数与所有截断输入 | 通过 |
| REC-12 缓存/Store/汇编 | Record 重复加载、显式 Store 策略、schema 1、规范模块的汇编逐字节往返 | 通过 |
| REC-13 两别名与来源位置 | 同源码两别名生成 `a_点` / `b_点` 两个类型；共享依赖无误报循环；同名字段不被改写；导入错误定位回原文件 | 通过 |
| REG-01 既有链路 | 配置公开结果 Map、CLI 逐行完整输出、旧 ABI 测试、自举 B0=B1=B2、包内示例与绑定验证 | 通过 |

18 组前端专项用例在每个正向源码上比较两个编译器输出；非法输入同时检查错误状态与定位。
自举门禁还用初始和重建编译器执行“两别名”及“字段与声明同名”的源码用例，避免仅验证恢复前端。

## 本轮修复

- 兄弟源码导入共享了可变祖先列表，导致同一路径的第二别名、或菱形依赖误报循环。
  现在每次递归构造独立路径列表；真正的循环仍由原拒绝测试约束。
- 命名空间改写将与类型/函数同名的构造字段或点访问字段一起加前缀。
  现在排除冒号前和点后的字段名，保留类型/函数引用的改写。

两项均先加入复现用例，再修复自举编译器；没有增加 recovery fallback。

## 配置工具迁移

`config_validator.ku` 的内部 `校验错误` Record 有四个必填标量字段：行号、字段、代码、消息。
`记录错误` 将 Record 放入内部列表；`校验` 返回前逐项调用 `导出错误`，创建原来的普通 Map。
公开返回仍为 `{ok, errors}`，每个错误仍只有 `line/key/code/message` 四个字段。

`ku-config` 在自己的 VM 上明确开启 `DAO_FEATURE_RECORDS` 后执行随包安装的 v3 模块。
没有改变通用 VM、Store 或绑定的默认能力。输出、行号、错误顺序和退出码仍由完整 CLI
断言约束；不把 Record 传给文本 Host，不要求 C++ 理解内部类型表。
新增语言用例检查修改导出 Map 不会改变原 Record 或第二次导出的 Map。这是示例适配检查，
不是 Schema 的 `to_map` 功能。当前仍未返回解析后的 `配置`，也未决定 workers 的缺省值。

## 验证范围

```powershell
. .\tools\activate_toolchain.ps1
cmake --build kernel/out/cmake --parallel 2
ctest --test-dir kernel/out/cmake --output-on-failure --parallel 2 -E aot_performance_gate
ctest --test-dir kernel/out/record-asan --output-on-failure --parallel 2 -R '^(record_conformance|record_tooling|record_store|ku_config_cli)$'
```

独立 ASan/UBSan 的内核 Record、工具往返、存储和配置 CLI 测试 4/4 通过。
最终 Windows 完整功能 CTest 75/75 通过（71.24 秒），其中自举 B0=B1=B2 门禁通过。
配置语言用例由 9 个增至 12 个；两个前端分别运行，且校验器模块字节一致。
SDK 验证检查实际安装的校验器为 v3/ABI11，运行两套源码测试，并验证破坏包内模块会失败，
不会绕回源码树。绑定、Store 和 AOT 拒绝路径继续随安装包回归。
解压 SDK 的隔离 PATH 验证已通过，同时用包内两套编译器检查别名与字段同名场景。

## 尚未交付与已知限制

- SCH-01/SCH-02 的通用 Schema 校验/转换尚未实现；示例显式导出独立 Map 不计为 Schema 完成。
- RES-01/RES-02 的 Result/Option、错误分支访问和载荷检查尚未实现。
- 无环嵌套 Record、跨模块类型签名、Host 构造器、容器泛型、Typed IR 和运行值持久化仍属后续阶段。
- 初次验收记录的 `keys(Map)` 差异已在后续兼容修复中解决：自举前端使用完整四字节名称匹配，
  `keysx` / `keys_` 不再误作内建函数。5 组 `ku_keys_frontends` 用例和自举双代用例覆盖此项。
  配置示例继续保留原有 Map 遍历实现。
- 本轮未运行 Linux/macOS 或独立 AOT 性能阈值；功能测试通过不等于发行或跨平台验证完成。
