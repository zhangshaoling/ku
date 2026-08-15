# Ku 语言系统文档入口

Ku 是语言系统和产品名称。现有 `dao_*`、Dao Binary Module、`dao/` 等名称是当前实现与历史 ABI 标识，不能据此把项目拆成两门语言。

项目执行入口：

- [`KU_NAMING_AND_AUTHORITY.md`](KU_NAMING_AND_AUTHORITY.md)：Ku 产品命名、实现分层和文档权威顺序。
- [`KU_V1_SEMANTICS.md`](KU_V1_SEMANTICS.md)：Ku v1 源语言语义、兼容层、推迟项和一致性门禁。
- [`KU_SUBPROJECT_WORKSHEETS.md`](KU_SUBPROJECT_WORKSHEETS.md)：Ku 小项目边界、依赖与验收工作表。
- [`KU_PROJECT_PROGRESS.md`](KU_PROJECT_PROGRESS.md)：当前项目进度、证据、风险和下一阶段。
- [`KU_LONG_TERM_ROADMAP.md`](KU_LONG_TERM_ROADMAP.md)：模型无关通用语言的长期目标、阶段计划与能力依赖。
- [`DAO_KERNEL_IMPLEMENTATION_GUIDE.md`](DAO_KERNEL_IMPLEMENTATION_GUIDE.md)：新内核唯一实施指导。

项目宪法和架构文档提供背景约束；旧阶段计划、C VM 接管说明和 Agent/Memory 路线是迁移证据，不得继续指导新内核实现。

现有 `dao/`、`ku/`、旧 `.ku` 标准库和 Python 测试属于迁移输入。新生产内核位于 `kernel/`。
