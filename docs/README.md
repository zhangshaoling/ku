# Ku 语言系统文档入口

Ku 是语言系统和产品名称。现有 `dao_*`、Dao Binary Module、`dao/` 等名称是当前实现与历史 ABI 标识，不能据此把项目拆成两门语言。

项目执行入口：

- [快速开始](../README.md#快速开始)：平台构建、编译检查、格式化和原生代码记忆运行。
- [内核使用说明](../kernel/README.md)：C ABI、默认自举前端、工具和 SDK。
- [原生测试命令](KU_TESTING.md)：v1.2.0 开发中的函数发现、失败报告和 CI 退出码。
- [`KU_NAMING_AND_AUTHORITY.md`](KU_NAMING_AND_AUTHORITY.md)：Ku 产品命名、实现分层和文档权威顺序。
- [`KU_V1_SEMANTICS.md`](KU_V1_SEMANTICS.md)：Ku v1 源语言语义、兼容层、推迟项和一致性门禁。
- [`KU_SUBPROJECT_WORKSHEETS.md`](KU_SUBPROJECT_WORKSHEETS.md)：Ku 小项目边界、依赖与验收工作表。
- [`KU_PROJECT_PROGRESS.md`](KU_PROJECT_PROGRESS.md)：当前项目进度、证据、风险和下一阶段。
- [`KU_LONG_TERM_ROADMAP.md`](KU_LONG_TERM_ROADMAP.md)：模型无关通用语言的长期目标、阶段计划与能力依赖。
- [`DAO_KERNEL_IMPLEMENTATION_GUIDE.md`](DAO_KERNEL_IMPLEMENTATION_GUIDE.md)：新内核唯一实施指导。

接口与集成指南：

- [模块与存储契约](../kernel/MODULE_ABI.md)：identity/version、链接、manifest、来源不可变、写锁与恢复。
- [标准库 ABI](../kernel/STDLIB_ABI.md)：模块身份、精确依赖、公开函数和跨模块值边界。
- [FFI](../kernel/FFI.md) 与 [所有权](../kernel/OWNERSHIP.md)：宿主能力、值的生命周期和容器使用。
- [工具链](TOOLCHAIN.md)：Windows 托管工具链的安装、版本和检查方式。
- [Agent/MCP 接入](AGENT_MCP_ONBOARDING.md)：新内核工具接入与当前 Python 桥接入口。

开发记录与发布：

- [v1.1.0 版本说明](releases/v1.1.0.md) 汇总代码记忆、工具命令、安装包和修复。
- [发布流程](RELEASING.md) 说明递增版本、标签和三平台安装包发布。
- [Releases](https://github.com/zhangshaoling/ku/releases) 提供已发布版本，开发分支的最新状态见 [Actions](https://github.com/zhangshaoling/ku/actions)。

<details>
<summary>历史记录与迁移背景</summary>

- [历史归档](archive/README.md)
- [2026-11-21 同步审查](KU_SYNC_REVIEW_2026-11-21.md)
- [PR #30](https://github.com/zhangshaoling/ku/pull/30)

项目宪法和架构文档提供背景约束；旧阶段计划、C VM 接管说明和 Agent/Memory 路线是迁移证据，不得继续指导新内核实现。

现有 `dao/`、`ku/`、旧 `.ku` 标准库和 Python 测试属于迁移输入。新生产内核位于 `kernel/`。

</details>
