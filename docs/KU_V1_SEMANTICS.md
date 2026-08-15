# Ku v1 语义矩阵

> 状态：当前权威基线  
> 决策日期：2026-07-14  
> 适用范围：Ku 源语言、恢复编译器、自举编译器和一致性语料

## 0. 规范语法：中文唯一

Ku v1 的**唯一规范表层语法是中文**。所有示例、教程、README 与对外入口都只以中文关键字呈现：

```ku
思 加一(x) { x + 1 }
```

英文关键字（`thought`、`func`、`if`、`else`、`return`、`import`、`while`、`for`、`break`、
`continue`、`throw`、`try`、`catch`、`true`、`false`、`null`）与中文关键字编译到**完全相同的字节码**，
因此在矩阵中仍是「核心」语义；但作为**呈现规范**，它们与 `func` 同级降为「兼容」：新代码不应使用，
不在任何入口、示例或 README 中作为首选形式展示，未来可经迁移周期删除。

矩阵条目如 `thought name(args)` 与 `思 name(args)` 仅表示二者字节码语义相同；规范写法一律以中文在前。

## 1. 裁决边界

本文定义 Ku v1 的源语言语义。二进制格式、VM 指令和 C ABI 分别由
`kernel/FORMAT.md`、`kernel/OWNERSHIP.md` 和 `kernel/FFI.md` 定义。

Ku v1 采用新内核恢复编译器已经验证的语义作为规范目标，并单独记录自举编译器缺口。
不采用“两套编译器当前能力的最小交集”，因为那会通过缩小规范隐藏迁移工作。

状态含义：

- **核心**：Ku v1 的稳定语义，所有合规前端最终都必须实现。
- **兼容**：恢复旧源码所需；新源码不应依赖，未来可经迁移周期删除。
- **推迟**：不属于 Ku v1；实现不得把它伪装成稳定语义。
- **实验**：AI 机器语上层的设计输入，不是当前源语言承诺。

表中的“恢复”指 `kernel/src/ku_migration.cpp`，“自举”指
`kernel/selfhost/compiler.ku`。`是`表示已有可执行证据，`缺口`表示后续项目必须补齐。

## 2. 值与运算

| 主题 | Ku v1 裁决 | 级别 | 恢复 | 自举 | 证据 |
|---|---|---|---|---|---|
| 空值 | `null` 与 `空`是同一值，不等于其他类型 | 核心 | 是 | 是 | `ku_migration`、`ku_selfhost_seed` |
| 整数 | 仅有有符号 `i64` 十进制整数；溢出是结构化运行时错误 | 核心 | 是 | 是 | `kernel_conformance`、`ku_selfhost_seed` |
| 浮点 | 无浮点字面量、值标签或隐式提升 | 推迟 | 明确拒绝 | 明确拒绝 | `ku_migration`、VM ABI v9 |
| Trit | `true/真 = +1`，`false/假 = -1`；Host 可提供 `0` | 核心 | 是 | 是 | `ku_migration`、`ku_selfhost_seed`、`kernel_conformance` |
| 逻辑 | `not/and/or` 只接受 Trit；`not = -x`，`and = min`，`or = max`，参数按从左到右求值，不短路 | 核心 | 是 | 是 | `ku_migration`、`kernel_conformance` |
| 条件 | `if`、`while` 的条件必须是 Trit；整数、字符串和容器没有隐式真值 | 核心 | 是 | 是 | `ku_migration` 的 strict-condition 反例 |
| 算术 | `+ - * / %` 只接受 `i64`；除法向零截断；除零和溢出报错 | 核心 | 是 | 是 | `kernel_conformance`、`ku_selfhost_seed` |
| 比较 | `< <= > >=` 只接受 `i64`；结果为 Trit | 核心 | 是 | 是 | `ku_migration`、`ku_selfhost_seed` |
| 相等 | `null`、Trit、字符串、字节和 `i64` 可判等；跨类型只支持 `==/!=`；容器不做深比较 | 核心 | 是 | 部分 | `ku_migration`、`kernel_conformance` |
| 字符串 | 字符串值必须是严格 UTF-8；规范转义为 `\n \t \r \" \\`；其他转义不属于 v1 | 核心 | 是 | 是 | `ku_migration`、`ku_selfhost_seed`、`kernel_conformance` |
| 显式转换 | `string(value)` 由 Host capability 提供显式值到字符串转换；不发生隐式数值提升 | 核心 | 是 | 是 | `ku_std_string`、`ku_selfhost_seed` |
| 字节 | `bytes` 只能由 Host/C ABI 提供，没有源语言字面量 | 核心 | 是 | 是 | `kernel_conformance`、`pure_c_abi` |

## 3. 容器、函数与生命周期

| 主题 | Ku v1 裁决 | 级别 | 恢复 | 自举 | 证据 |
|---|---|---|---|---|---|
| List | 列表有序、可变；索引必须是非负 `i64`；越界和负索引报错 | 核心 | 是 | 是 | `ku_migration`、`kernel_conformance` |
| Map | 字面量键必须是字符串；动态访问键也必须是字符串；缺失键返回 `null` | 核心 | 是 | 是 | `ku_migration`、`ku_selfhost_seed` |
| 属性访问 | `object.field` 是 `object["field"]` 的语法糖，当前适用于字符串键 Map；缺失字段返回 `null` | 核心 | 是 | 是 | `ku_migration`、`ku_selfhost_seed` |
| 索引赋值 | `list[i] = value` 与 `map[key] = value` 原地修改当前调用代际的容器 | 核心 | 是 | 是 | `ku_migration`、`ku_selfhost_seed` |
| 追加 | `push(list, value)` 是规范追加操作并返回同一 List | 核心 | 是 | 是 | `ku_migration`、`ku_selfhost_seed` |
| 容器长度 | `len(value)` 接受 List 或 Map，返回 `i64` 长度；其他值是类型错误 | 核心 | 是 | 是 | `ku_migration`、`kernel_conformance` |
| Map 键枚举 | `keys(map)` 返回按 UTF-8 字节序排序的字符串 List；其他值是类型错误 | 核心 | 是 | 是 | `ku_migration`、`ku_selfhost_seed` |
| 函数引用 | 本模块函数名可作为值，并可在同一次顶层调用中调用；Host capability 不能作为函数值 | 核心 | 是 | 是 | `ku_selfhost_seed`、`ku_std_type`、`ku_std_list` |
| 绑定 | `bind(function, captured...)` 创建同一次顶层调用内有效的绑定 | 核心 | 是 | 是 | `ku_selfhost_seed`、`ku_std_type`、`ku_std_list`、`kernel_conformance` |
| 持久闭包 | 闭包跨顶层调用、跨 VM 或持久化后继续执行 | 推迟 | 拒绝复用 | 拒绝复用 | `kernel/OWNERSHIP.md`、`kernel_conformance` |
| 容器生命周期 | VM 返回的 List/Map/Closure 只在当前 generation 有效；下一次已接受调用使其失效 | 核心 ABI 边界 | 是 | 是 | `kernel/OWNERSHIP.md`、`kernel_conformance` |

`list + [value]` 仍由恢复编译器接受并原地追加，但它是兼容写法；新代码使用
`push(list, value)`，不能把 `+` 理解成通用容器复制或拼接。

## 4. 声明、作用域与控制流

| 主题 | Ku v1 裁决 | 级别 | 恢复 | 自举 | 证据 |
|---|---|---|---|---|---|
| 函数声明 | 顶层 `thought name(args) { ... }` 与 `思 name(args) { ... }` 语义相同；参数个数固定且参数名不得重复 | 核心 | 是 | 是 | `ku_migration`、`ku_selfhost_seed` |
| 局部变量 | 参数和赋值创建函数级局部绑定；块不创建新作用域；使用未定义名字是编译错误 | 核心 | 是 | 是 | `ku_migration`、`ku_selfhost_seed` |
| 求值顺序 | 表达式、调用参数、List 项和 Map 值从左到右求值 | 核心 | 是 | 是 | `ku_migration`、`ku_selfhost_seed` |
| 返回 | `return/返 expr` 显式返回；函数最后一个表达式可隐式返回。v1 程序不得依赖空函数或控制流落空的默认值 | 核心 | 是 | 是 | `ku_migration`、`ku_selfhost_seed` |
| 分支 | `if/若 ... { ... } else/否 { ... }`；允许 `else if` 与 `否 若` | 核心 | 是 | 是 | `ku_migration`、`ku_selfhost_seed` |
| 循环 | `while/当`、List 上的 `for/遍 item in/于 list`、Map 上的 `for/遍 key, value in/于 map`、`break/断`、`continue/续`；循环外使用 `break/continue` 是编译错误 | 核心 | 是 | 是 | `ku_migration`、`ku_selfhost_seed` |
| 异常 | `throw/抛 value` 可跨本模块函数传播；`try/试 ... catch/捕 name` 只捕获显式抛出的值，不捕获类型/校验错误；`std/error.ku` 约定错误值为 `{ "ok": false, "code": ..., "message": ... }` Map | 核心 | 是 | 是 | `ku_migration`、`ku_selfhost_seed`、`ku_compile_std_error` |
| 注释与分隔 | `//`、`;;` 为行注释；换行和 `;` 都可分隔语句 | 核心 | 是 | 是 | `ku_migration`、`ku_selfhost_seed` |

`func`、无 `else/否` 的裸备用块、前缀运算符调用（如 `>= (a, b)`）、变参
`and(...)`/`or(...)` 和值形式 `if (...) { value } { alternate }` 属于兼容语法。
它们不得成为新标准库 API 的唯一写法。

## 5. 调用、Host 与模块

| 主题 | Ku v1 裁决 | 级别 | 恢复 | 自举 | 证据 |
|---|---|---|---|---|---|
| 本地调用 | 函数可前向引用和递归；参数个数在编译期校验 | 核心 | 是 | 是 | `ku_migration`、`ku_selfhost_seed` |
| Host 导入 | `import name(arity)` 声明固定参数个数的显式 Host capability；调用走数值 C ABI | 核心 | 是 | 是 | `ku_migration`、`ku_selfhost_seed` |
| 记忆原语 | `存 key value` 写入持久 i64，返回 value；`取 key` 读取 i64，不存在返回 0；`忘 key` 删除并返回 1/0；运行时由 `memory_store/recall/forget` Host capability 提供 | 实验 | 是 | 是 | `ku_migration`、`ku_selfhost_seed`、方向二 memory Host |
| 源模块导入 | `import "path" as alias` 与 `引 "path" 别 alias` 在编译期递归组合源码；路径受模块根限制并检查循环 | 核心 | 是 | 是 | `ku_migration`、`ku_selfhost_seed`、`ku_compile_recursive_import`、`ku_reject_import_*` |
| 运行时模块 ABI | 独立编译模块之间的动态链接、版本解析和运行时状态共享 | 推迟 | 无 | 无 | KU-P10 |

当前源码组合使用 `alias_function` 作为导入调用名。这是 Ku v1 的临时模块语义，不能被描述成已经完成的运行时模块 ABI。

`dao-ku --check input.ku` 只执行恢复或自举编译、模块验证和诊断，不写出 `.dao` 文件；默认走自举编译器，传入 `--recovery` 时走恢复编译器。成功输出 `checked <input.ku>`，失败输出稳定的编译阶段和错误信息。

## 6. AI 机器语边界

`thought/思` 在 Ku v1 中首先是可调用函数声明。Thought schema、canonical AST、trace、
patch、记忆留存、晋升和工具暴露属于 KU-P11 至 KU-P14 的实验语义。旧
`docs/AGI母语语义内核规范.md` 和 `dao/std/semantic_core.ku` 是迁移输入，不能覆盖本文的源语言裁决。

因此，Ku v1 不承诺：

- 声明一个 `thought` 就自动持久化或成为工具；
- Python callable、Python 对象或 JSON 形状成为语言值；
- 自修改 AST、异步并发或自动记忆强化是内建语言语义。

## 7. 一致性门禁

恢复编译器的必跑门禁：

```powershell
ctest --test-dir kernel/out/cmake -R "^(ku_migration|ku_compile_acceptance|ku_compile_legacy_import|ku_reject_import_escape|ku_std_type|ku_std_list)$" --output-on-failure
```

自举编译器的当前门禁：

```powershell
ctest --test-dir kernel/out/cmake -R "^ku_selfhost_seed$" --output-on-failure
```

`ku_selfhost_seed` 通过证明当前 SH2 表面稳定，不表示它已经完整实现本矩阵；该测试包含 bootstrap、rebuilt 和 rejection parity，运行时间较长。
生产路径的递归、越界、缺失和循环导入另由 `dao-ku` 的定向 CTest 覆盖。

任何语义变更必须同时修改本文、至少一个正例或反例测试，并说明它是兼容变更还是 Ku v2 变更。历史 Python/旧 C VM 行为只能作为候选输入，不能直接推翻矩阵。
