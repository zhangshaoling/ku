# Ku — 可执行记忆语言

```text
思 = 代码 = 记忆
```

Ku 是一门为 AGI 而生的语言：一个「念头」同时是可执行的代码、可检查的数据结构、
可持久化的记忆、可被智能体通过 MCP 调用的工具。

规范语法是中文：

```ku
思 main(x, y) {
  返 x + y
}
```

## Ku v1

语义基线已冻结（[`docs/KU_V1_SEMANTICS.md`](docs/KU_V1_SEMANTICS.md)），新内核三平台 CI 全绿。

### 关键字

| 中文 | 含义 | 中文 | 含义 |
|---|---|---|---|
| `思` | 定义函数 | `返` | 返回 |
| `若` / `否` | 分支 | `当` | 循环 |
| `遍 … 于` | 遍历 | `断` / `续` | break / continue |
| `试` / `抛` / `捕` | 异常 | `引 … 别` | 导入模块 |
| `真` / `假` | Trit（+1 / -1） | `空` | null |

英文关键字（`thought`、`if`、`return`、`import` …）编译到完全相同的字节码，仅为兼容期保留，
不在任何入口展示。

### 例子

```ku
思 分类(x) {
  若 x > 10 { 返 3 }
  否 若 x > 0 { 返 2 }
  否 { 返 1 }
}

思 求和(items) {
  total = 0
  遍 item 于 items { total = total + item }
  返 total
}
```

## 运行

```powershell
.\tools\build_kernel.ps1
.\kernel\out\cmake\bin\dao-ku.exe input.ku output.dao
```

## 内核（实现）

`kernel/` 是新内核的实现权威：

```text
.ku 源码 → 编译器 → .dao 二进制模块 → 校验器 → Register VM → C ABI → 结果 / 记忆 / 工具
```

- 确定性二进制模块（v2 / ABI10；v1 / ABI9 兼容）
- 严格 section / 指令校验器
- `i64` 算术（检查溢出）、Trit 三值逻辑
- C ABI / FFI / AOT / Python·Rust 绑定
- 可执行记忆：每个记忆就是一个可调用的 Thought

详见 [`kernel/README.md`](kernel/README.md)。

## 可执行记忆闭环

一个念头：写下来是代码，存起来是记忆，召回就能跑，注册了就是工具。

```powershell
.\tools\build_kernel.ps1
.\.venv\Scripts\python.exe demos\memory_loop.py
```

```text
写 .ku → 编译 .dao → 存记忆(持久化) → 换进程召回 → 执行 → MCP 暴露 → Agent think-act-observe
```

MCP 服务暴露 `ku_memory_store` / `ku_memory_recall` / `ku_memory_search` / `ku_memory_stats`，
让记忆成为可被智能体直接调用的工具。

## 权威文档

- [命名与权威](docs/KU_NAMING_AND_AUTHORITY.md)
- [Ku v1 语义](docs/KU_V1_SEMANTICS.md)
- [项目进度](docs/KU_PROJECT_PROGRESS.md)
- [二进制格式](kernel/FORMAT.md)

## 旧线

`dao/`（旧 C VM）与 `ku/`（更早的 Python 实现）保留为迁移输入，不再承载新功能。

MIT License
