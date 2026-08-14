# Ku 命名与文档权威决策

> 决策日期：2026-07-14
> 状态：当前有效
> 适用范围：产品、语言、架构、路线、项目管理和对外说明

## 1. 唯一语言名称

本项目的语言系统和产品名称是 **Ku**。

Ku 的目标是构建 AI 机器语系统，使 Thought 能够成为可执行代码、结构化数据和持久记忆。文档不得再把 Ku 和 Dao 描述成两门并列语言，也不得因目录或 ABI 中存在 `dao` 名称而另建一条产品路线。

## 2. Dao 名称的当前含义

`Dao` 暂时只用于已经存在的内部实现和兼容标识，包括：

- `dao_*` C ABI 符号；
- Dao Binary Module 等已经冻结或版本化的格式名称；
- `dao/` 旧 C VM、旧自举前端和 AI 机器语原型目录；
- 已有二进制、环境变量、测试夹具和历史文档中的兼容名称。

这些名称不改变 Ku 的产品身份。是否重命名内部 ABI、格式或目录必须单独立项，完成兼容性、迁移和版本决策后执行。本项目 KU-P00 不做代码级批量重命名。

## 3. 实现分层

```text
Ku 语言系统
  ├─ kernel/       新生产内核：模块、Verifier、Register VM、C ABI、AOT、SDK
  ├─ kernel/selfhost + kernel/stdlib
  │                Ku 自举编译器和迁移中的标准库
  ├─ dao/          旧 C VM、旧前端、Thought/Memory/MCP 原型，作为迁移输入
  └─ ku/           更早的 Python/Ku 兼容实现和历史语料
```

`kernel/` 是新生产内核的实现权威。`dao/` 和 `ku/` 中尚未迁移的能力可以继续作为可执行原型和对拍依据，但不能成为新内核的隐藏依赖。

## 4. 文档权威顺序

出现冲突时，按以下顺序裁决：

1. `docs/KU_NAMING_AND_AUTHORITY.md`：产品名称、实现分层和文档权威。
2. `docs/KU_V1_SEMANTICS.md`：Ku v1 源语言语义与兼容边界。
3. `docs/KU_SUBPROJECT_WORKSHEETS.md`：整体项目边界、依赖和验收条件。
4. `docs/KU_PROJECT_PROGRESS.md`：当前实测状态、风险和下一工作单。
5. `docs/DAO_KERNEL_IMPLEMENTATION_GUIDE.md`：新内核实现原则；文件名保留兼容名称。
6. `kernel/FORMAT.md`、`kernel/FFI.md`、`kernel/OWNERSHIP.md`、`kernel/MIGRATION.md`：格式、FFI、所有权和迁移契约。
7. `docs/KU_MIGRATION_PLAN.md`、`docs/KU_SELFHOST.md`：Ku 迁移编译器和自举路径。
8. 其他旧 C VM、MCP、Memory、K6-K8 和历史阶段文档：迁移证据或设计输入。

任何文档声明都不能覆盖当前代码和可重复测试结果。进度状态必须回写到 `KU_PROJECT_PROGRESS.md`。

## 5. 标准术语

| 场景 | 使用名称 |
|---|---|
| 语言、产品、生态、语法 | Ku |
| 整体项目 | Ku 语言系统 / Ku AI 机器语系统 |
| 新执行内核 | Ku 新内核；需要精确指代码时写 `kernel/` |
| C ABI | Ku C ABI（当前符号前缀为 `dao_*`） |
| 二进制格式 | Dao Binary Module（当前冻结的内部格式名） |
| `dao/` | Ku 旧 C VM 与 AI 机器语迁移输入 |
| `ku/` | Ku 历史兼容实现 |

## 6. 变更规则

- 新文档默认使用 Ku，不再使用“Ku / Dao”作为产品名。
- 引用内部实现时可以保留 Dao，但必须说明它是实现标识。
- 不因命名统一顺手修改 ABI、磁盘格式、环境变量、工具名或目录。
- 后续若决定清理 Dao 内部名称，必须建立独立兼容迁移项目。
