# Ku — 模型无关的智能系统通用编程语言

```text
模型负责能力，Ku 负责结构、契约、流程与执行
```

Ku 是一门模型无关的通用编程语言：统一描述普通程序、不同类型的模型、工具、
知识、任务和可恢复执行。云端模型、本地模型、视觉模型、语音模型、推理模型和未来模型
都应通过统一的 Ku 能力契约被组合、验证和替换。

Ku 的长期目标不是研发某一个模型，而是成为所有模型都可以使用的通用语言。
长期路线见 [`docs/KU_LONG_TERM_ROADMAP.md`](docs/KU_LONG_TERM_ROADMAP.md)。

当前迁移阶段仍保留“思 = 代码 = 记忆”的历史定位，但它不是 Ku 语言能力的全部。

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
- 当前迁移阶段支持可执行记忆：每个记忆可以是一个可调用的 Thought
- 长期方向是 Model Capability、Schema、任务、流式结果和可恢复执行

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
