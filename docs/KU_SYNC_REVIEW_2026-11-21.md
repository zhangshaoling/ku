# Ku 本地修复与 GitHub 同步审查

审查日期：2026-11-21（Asia/Shanghai）。以下为本地工作区证据，不是已发布版本声明。

## 远端与本地

- 仓库：<https://github.com/zhangshaoling/ku>。
- 远端 `main`：`eeb080ad7283d0b84bd488579f90fd1282937b76`。
- 审查开始时，远端及本地 `ku-language-foundation`：`6c642ae74daa9010b28a37b04226c2c0a88bf085`，领先 main 一个 `ku check` 提交，没有落后提交。
- 接手时另有 23 个已修改或新增文件未提交，涉及原生模块存储、格式化、Python Host/MCP 和文档。这些内容尚未同步到 GitHub。
- 查询时没有开放中的 PR。
- 历史 [CI run 31774870246](https://github.com/zhangshaoling/ku/actions/runs/31774870246) 的 `test` 与三平台 kernel job 均成功；对应提交 `1db456a`，不能替代当前改动的 CI。

## 本轮修复

| 问题 | 回归与修复 |
|---|---|
| 相同模块已存在但 manifest 缺失时，保存假成功 | 先用 `module_store` 测试复现失败；重试现在恢复缺失 manifest，并校验已有 manifest，不匹配时返回 `DAO_BAD_MODULE` |
| CLI 把 `1.0.0junk` 当作合法版本 | 先用 CLI 回归复现；改为完整消费三个 uint32 数字段，拒绝尾随字符、负数、空段和溢出 |
| 中文 `思 main(x)` 被推断为无参数 | 先用持久化闭环断言复现；参数推断支持规范中文及英文兼容声明，测试验证存储、召回后的参数元数据 |
| 状态文档互相矛盾 | 更新模块 ABI 语义矩阵；P11–P14 改为进行中，记录具体未完成边界与最新本地证据 |

## 验证

- 低并行度构建通过。
- `ctest --test-dir kernel/out/cmake --output-on-failure -E aot_performance_gate`：55/55 通过，27.12 秒。
- CI 选用的六个 Python 测试文件：21/21 通过，包含中文参数持久化回归。
- SDK 安装通过；`tools/test_installed_sdk.ps1` 的独立 CMake consumer 配置、构建和执行通过。
- 本轮未执行性能阈值、全量旧链 pytest、fuzz、soak 或新改动的 Linux/macOS CI。

## 同步建议

用户已确认将代码、回归测试及规范文档提交到 `codex/ku-memory-tooling-20261121`，并创建草稿 PR 验证当前改动。代码/测试与状态文档分为两次提交；审查范围如下：

1. 原生 ModuleStore、`dao-module`、CMake 集成、恢复/版本回归和 ABI 说明。
2. Python 标量记忆 Host、CodeMemory、Runtime/MCP 接线、中文 Thought 参数修复与测试。
3. 已有格式化命令、check/fmt 门禁和相关语义说明。
4. 当前进度、工作表和同步审查记录。

会话交接记录可保留在本地；构建输出、工具链、数据库、记忆数据与临时实验不属于本次同步范围。

合入 main 前需要当前提交的新 CI 结果，并明确实验边界：ModuleStore 尚未提供多文件事务、跨进程并发写入保证和完整来源链契约；Agent/TaskPlanner 仍有规则选择及占位规划；Python 安装入口、SDK 完整发行和旧链退役尚未统一。本次同步应保留这些未完成项，不能宣称整个长期路线已完成。
