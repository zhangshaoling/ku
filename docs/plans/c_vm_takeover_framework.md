# C VM 接管框架（Phase 4 → Phase 5）

> 给后续模型/后续会话看的接力文档。
> 核心原则：小步、备份、对拍、提交；对拍门禁是地基，永不变红。
> 本文档只规划 C VM 从「对拍验证过的影子」走到「玄璃真正用的 runtime」的路径。

## 当前基线

仓库路径：`D:\Tools\Dao`，分支 `bootstrap-ku-bytecode`。

当前 checkpoint：

```text
7e0e68b feat: self-host frontend bootstrap regeneration via C VM
```

当前状态（2026-06-15 实测）：

- `python dao/verify_core.py`：22 项核心自检通过（唯一失败的「网络测试」是预期的，本机需走 18888 代理，不算 Dao 故障）。
- `pytest`：214 测试全绿 = 81 Python/Ku + **133 C VM 对拍**（经 WSL 真实编译运行 `dao/dao_core.c`）。
- C VM 已具备 P0/P1 bytecode 对拍能力，能端到端自举并运行 `.ku` 前端，甚至在 VM 内重新生成自己的 bootstrap 镜像。

## 北极星（什么叫「做完」）

玄璃的执行路径从 Python DaoVM 切到 C VM，且满足：

1. Windows **原生**跑（无 WSL）。
2. 经验层 `experience.ku` / MCP 网关在 C VM 上跑通。
3. 长进程**不漏内存、不 `exit(1)`**。
4. 对拍门禁全绿，且打的是**原生二进制**。

---

## 固定工作循环（每一步都照做）

```bash
cd /d/Tools/Dao
mkdir -p backups
ts=$(date +%Y%m%d_%H%M%S)
tar -czf backups/dao_before_<任务名>_${ts}.tar.gz dao

# 只做一个窄改动

python dao/verify_core.py
python -m pytest tests/test_c_vm_parity.py -q

# 只有全绿才做 after 备份
ts=$(date +%Y%m%d_%H%M%S)
tar -czf backups/dao_after_<任务名>_${ts}.tar.gz dao

git add <本次真正修改的文件>     # 只提交本次相关文件，不要混入旧临时文件
git commit -m "<简短提交信息>"
```

**对拍是地基，永不变红。每个里程碑结束，parity 计数只增不减。**

每个 session 开场先跑：

```bash
git status --short && git log --oneline -5 && python dao/verify_core.py
```

如果核心自检不通过，先修核心自检，不做新功能。

---

## 已核查的真实工作量（2026-06-15）

| 项目 | 实测 | 含义 |
|---|---|---|
| `exit()` 调用 | 13 处 | 结构化错误是有界任务 |
| 分配 vs 释放 | ~71 `malloc/strdup/realloc/calloc` / 75 `free` | 已有释放，但所有权无系统纪律 → 大件 |
| POSIX 阻塞点 | 仅 `popen` / `getcwd` / `mkdir` 三处 | 原生 Windows 构建很便宜 |
| sqlite | `dao/std/experience.ku` 重度依赖（经验层 + MCP 网关） | 接管经验层必须补 |
| `并行执行` | 仅 `dao/std/async.ku` 3 处 | 不急，可延后 |

---

## 里程碑

依赖关系：**M0 → M1 → {M2, M3 可并行} → M4 → M5**。M1 是杠杆点，先砸下去，后面全程受益。

### M0 — 收尾异常/边界对拍 `热身 · 0.5天 · 低风险`

- **目的**：动 C 之前先把 bytecode 对拍封顶，留足安全网。
- **动作**：补审计已列的 4 类用例 → `try_finally_shape` / `try_catch_call_unknown_builtin` / `try_catch_string_index_out_of_range` / `try_catch_set_index_error`，哪个红修哪个 opcode / 异常路由。
- **退出标准**：这 4 类绿，parity ≥ 137。

### M1 — 原生 Windows 构建 `1天 · 低风险 · 高杠杆`

- **目的**：dev loop 快一个数量级 + 门禁打真实目标 + 甩掉 WSL 依赖。
- **动作**：
  - 三个 `#ifdef _WIN32` 壳：`popen→_popen`、`getcwd→_getcwd`、`mkdir(p,mode)→_mkdir(p)`；`unistd.h` / `sys/stat.h` 用 ifdef 收口。（已确认全文件只有这三处 POSIX 专有调用。）
  - 加 `tools/build_dao_core.ps1`（mingw `gcc` 或 MSVC `cl`）→ 产出 `dao/dao_core.exe`。
  - 改 `tests/test_c_vm_parity.py` 的 `c_vm_cmd` fixture：优先原生 `dao_core.exe`，WSL 退为 fallback，去掉 `to_wsl_path` 依赖。
- **退出标准**：`dao_core.exe` 在 Windows 直接跑；parity 全绿且走原生二进制；`verify_core` 不受影响。

### M2 — 结构化错误 `1–2天 · 中风险 · 有界`

- **目的**：去掉 13 处 `exit(1)`，错误能传播 / 被 catch —— 常驻 runtime 和内存清理的前置。
- **动作**：复用已有 `ExecResult` 的 error 字段，13 处逐个改成结构化错误；只在 `main` 顶层翻译成非零退出 + 错误形状。补对拍：原来靠 exit 的错误形状，现在验证「能被上层 catch 且形状不变」。
- **退出标准**：`dao_core.c` 里 `exit()` 仅剩 main 顶层 1 处；未捕获错误退出码 / 形状不变；parity 全绿。

### M3 — sqlite 内置 `1–2天 · 中风险`

- **目的**：`experience.ku` 经验层 + MCP 网关脱离 Python 跑。
- **动作**：`sqlite3.c` amalgamation 进 `vendor/`（零外部依赖，Win/WSL 都能编）→ 实现 `sqlite_open` / `sqlite_exec` / `sqlite_query` / `sqlite_close`，`[?]` 参数绑定对齐 Python 侧语义 → 用 experience.ku 真实建表/插入/查询路径对拍 Python DaoVM。
- **退出标准**：experience.ku 全部入口在 C VM 与 Python 一致。

### M4 — 内存所有权模型 `大件 · 3–5天 · 高风险`

- **目的**：长进程不漏。放这里，是因为前面已有快循环（M1）+ ASan + 错误传播（M2）兜底。
- **选型**：倾向 **Value 引用计数**（env / 记忆 / 闭包是长持有，per-frame arena 不适用）。
- **已知硬点**：`env ↔ closure ↔ env` 会成环 —— 要么 env→closure 用弱引用断环，要么先接受环、后补 mark-sweep。框架里先标出来，别假装没有。
- **动作**：Value 加 `retain` / `release` 骨架 → 容器 / 函数 / 闭包接入 → valgrind（WSL）/ ASan（Win）清零泄漏。
- **退出标准**：典型脚本 + 经验层往返 0 泄漏；parity 全绿。

### M5 — 翻转执行路径 `Phase 5 · 真正接管`

- **目的**：MCP / runtime 默认用 C VM 执行。
- **动作**：`dao/mcp_server.py` 的 `ku_eval` / `ku_call` 走 C VM，Python DaoVM 退为 fallback / 对拍基准；灰度：先一个工具切 C，对比一致后全切。
- **退出标准**：MCP 网关默认 C VM；Python DaoVM 仅留作对拍基准。

---

## 横切纪律

- 中文别名**加壳**，不删旧函数。
- Python 只动解释器 / 解析器 / 运行时；天书逻辑写 `.ku`。
- 不一次做大工程、不先写 planner、内存模型前不碰性能优化。

## 可延后（择机插入）

- `并行执行`（async.ku 仅 3 处）：定调度语义后再做，M3 之后任意时点。
- JSON parser 加固（`\uXXXX` / 畸形输入 / 边界）：可在 M2 顺手补。

---

## 不要做的事

- 不要让对拍门禁变红来「先推进度」。
- 不要大规模重命名标准库 / 删旧英文函数。
- 不要在内存模型落地前做性能优化。
- 不要跳过备份和对拍验证。
- 不要把无关临时文件混进提交。
