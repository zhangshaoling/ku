# AGI 母语内核第一阶段

目标：先固定 AI 可操作的语义地基，再继续压缩表层语法。

## 当前判断

- `ku/` 是当前 Python 包入口，`pyproject.toml` 的 CLI 指向 `ku.runtime:main`。
- `dao/` 是当前更完整的实验地基，已有中文语法、核心自检、标准库别名、Memory/Tool 雏形。
- 本阶段先落在 `dao/`，不做 `ku`/`dao` 大迁移，不碰现有 C 运行时改动。

## 八个核心概念

1. `Node`：结构化 AST 节点，代码和数据的统一载体。
2. `Thought`：可执行记忆单元。
3. `Memory`：可持久、可索引、可演化的状态。
4. `Env`：thought、memory、tool、trace 的运行上下文。
5. `Effect`：一次读、写、调用、补丁等外部或内部影响。
6. `Trace`：按时间记录 effect、节点、结果和成功状态。
7. `Patch`：可审计、可反向应用的 AST 修改。
8. `Tool`：带风险和效果记录的外部能力入口。

## 第一阶段实现

新增 `dao/semantic_core.py`，保持独立：

- 复用 `dao.runtime.Node` 和 `Thought`。
- 提供 `Memory`、`SemanticEnv`、`Effect`、`Trace`、`Patch`、`ToolSpec`。
- 提供短步骤展开：

```python
thought_ast("fix_bug", [
    "observe tests.fail",
    {"locate": {"mode": "causal"}},
    {"patch": {"scope": "minimal"}},
    "verify",
])
```

展开为当前 parser/compiler 可识别的 canonical thought AST。

## 第二阶段实现

新增薄适配层，仍然不改 `dao/runtime.py`：

- `thought_from_ast(ast)`：把 canonical thought AST 转成现有运行时 `Thought`。
- `DaoSemanticAdapter`：连接 `SemanticEnv` 和现有 `DaoEnv`。
- `DaoSemanticAdapter.define_thought(...)`：短步骤 -> canonical AST -> `Thought` -> 注册到 `DaoEnv`。
- `DaoSemanticAdapter.register_tool(...)`：把 `ToolSpec` 暴露为运行时可调用函数，并保留 trace。

最小用法：

```python
from dao.runtime import DaoEnv
from dao.semantic_core import DaoSemanticAdapter, ToolSpec

dao_env = DaoEnv()
adapter = DaoSemanticAdapter(dao_env)

adapter.register_tool(ToolSpec("observe", lambda target: target))
adapter.register_tool(ToolSpec("verify", lambda: "ok"))
adapter.define_thought("check_state", [
    "observe system.ready",
    "verify",
])

dao_env.run("check_state", [])
```

这一步的意义：短语义不再只是 AST 数据，已经可以进入当前 Dao 运行时执行。

## 第三阶段实现

新增 `DaoSemanticAdapter.run_thought(name, args=None)`：

- 调用前记录 `thought.call`。
- 调用成功后记录 `thought.result`。
- 调用失败时记录 `thought.error`，并保留原异常向上抛出。

这一步不接管 `DaoEnv.run()`，只给通过 adapter 的执行路径加审计轨迹。

## 回退点

- 基线备份：`backups/agi_mother_tongue_baseline_20260613-002034`
- 本阶段改动前备份：`backups/before_semantic_core_20260613-002302`
- 第二阶段改动前备份：`backups/before_semantic_adapter_20260613-003450`
- 第三阶段改动前备份：`backups/before_semantic_run_trace_20260613-004155`

回退方式：

1. 删除新增文件：
   - `dao/semantic_core.py`
   - `tests/test_semantic_core.py`
   - `docs/plans/agi_mother_tongue_kernel.md`
2. 如后续改过同名文件，从对应 `backups/before_*` 目录复制回来。

## 下一步

- 为 `Patch` 增加 AST 路径搜索：按 thought 名、node type、ref 名定位。
- 把短语法展开规则暴露给 `.ku` 层，让中文 thought 也能生成 canonical AST。
- 给 `Trace` 增加筛选和导出：按 thought、effect kind、ok 状态查询。
