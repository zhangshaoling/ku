# Phase 5 接管进展接力

> 时间：2026-06-15 21:23 +08:00  
> 分支：`bootstrap-ku-bytecode`  
> 目标：玄璃执行路径从 Python DaoVM 切到 C VM。  
> 状态：Phase 5 未完成；本轮完成 M0，推进 M1/M2，开始审计 MCP 执行链。

## 当前工作树

本轮主线改动集中在：

- `dao/dao_core.c`
- `dao/compiler.py`
- `tests/test_c_vm_parity.py`
- `tools/build_dao_core.ps1`

仍有早先存在的本地未跟踪文件：

- `AGENTS.md`
- `CLAUDE.md`
- `inspect_wechat_sqlite.py`
- `scan_wechat_xlog.py`
- `wechat_login_diag.ps1`

这些不是本轮 Phase 5 主线改动，不要混入提交，除非后续任务明确需要。

## 已完成：M0 异常/边界对拍

C VM parity 从 `133` 增至 `137`，后续 M2 又增至 `139`。

新增/覆盖的边界：

- `try_finally_shape`
- `try_catch_call_unknown_builtin`
- `try_catch_string_index_out_of_range`
- `try_catch_set_index_error`
- `try_catch_unknown_binary_op`
- bytecode 级 `value is not callable` 可被 catch

Python 对拍 VM 同步修正：

- `BINARY_OP` / `UNARY_OP` 异常可进入当前 frame 的 catch handler。
- `SET_INDEX` 异常可进入 catch handler。
- `SET_INDEX` 不可赋值对象错误文本对齐 C VM：`object does not support item assignment`。
- 未知二元操作符错误文本对齐 C VM：`Unknown op: <op>`。

## 已推进：M1 Windows 原生构建

`dao/dao_core.c` 已加入 Windows 兼容壳：

- `_getcwd`
- `_mkdir`
- `_popen`
- `_pclose`
- `_findfirst` / `_findnext` 实现 Windows `list_dir`
- `strdup` 在 `_WIN32` 下映射为 `_strdup`

新增脚本：

```powershell
tools/build_dao_core.ps1
```

脚本行为：

- 优先用 `gcc` 构建 `dao/dao_core.exe`。
- 其次尝试 MSVC `cl`。
- 找不到原生 C 编译器时明确失败。

测试 fixture 已改为：

1. 先尝试运行 `tools/build_dao_core.ps1`。
2. 若 `dao/dao_core.exe` 存在且构建成功，则 C VM parity 使用原生 exe。
3. 否则回退 WSL `kali-linux` 下的 `/mnt/d/tmp/dao_core_parity_pytest`。

当前环境限制：

- PATH 中未找到 `gcc` / `cl` / `clang` / `clang-cl`。
- 因此 M1 的“原生 exe 实测 + parity 打原生二进制”尚未闭环。
- 当前 parity 仍通过 WSL fallback 验证。

## 已推进：M2 结构化错误

`dao/dao_core.c` 新增：

```c
static ExecResult exec_error(const char *message)
```

已将 `exec_frame` 中多处内部 `exit(1)` 改为：

- 若当前 frame 有 try handler：`frame_raise(...)` 进入 catch。
- 否则返回 `ExecResult { is_error = 1 }`，由调用者或 `main` 顶层翻译为非零退出。

已覆盖的结构化错误路径：

- `LOAD_NAME` 未定义
- 未知 `BINARY_OP`
- 函数调用内部错误传播
- `parse(...)` 内部错误传播
- 未定义 builtin/function
- 非 callable 调用
- list/string index out of range
- 不可索引对象
- list assignment out of range
- 不支持 item assignment
- try stack overflow
- 未知指令

剩余注意：

- `dao_core.c` 中 `main` 顶层仍会打印 `RuntimeError: ...` 并返回 `1`，这是预期的最终翻译层。
- 文件读取、import path 等启动/加载阶段仍直接向 stderr 报错并返回失败，这部分不属于 frame 内可 catch 语义，但后续若要做长进程 runtime，也应继续收口。

## 验证结果

本轮最后一次完整验证：

```text
python dao/verify_core.py
核心自检通过
```

```text
pytest tests/test_c_vm_parity.py -q
139 passed
```

```text
pytest -q
220 passed
```

注意：`verify_core.py` 中网络测试有时取决于本机代理状态；只要末尾为 `核心自检通过`，当前门禁视为通过。

## 刚开始审计：M5 MCP 执行链

用户指出“目标是脱离 Python，为什么还在用 Python”。这里的边界需要保持清楚：

- Python 作为测试框架、对拍基准、MCP stdio 胶水，短期仍可存在。
- Phase 5 真正要求的是 Dao/Ku 执行路径默认走 C VM，而不是 Python DaoVM / `Thought.call`。

当前发现：

- `dao/mcp_server.py` 的 `ku_eval` 仍通过 `_parse_expr` / `Thought("__mcp_eval__").call()` / `compile_道 + run_bytecode` 执行。
- `ku_call` 和经验层工具仍通过 `Thought.registry` + `thought.call(args)` 执行。
- `dao/runtime.py` 的 builtin `run_bytecode` 仍调用 Python `DaoVM().execute(bytecode)`。

下一步建议：

1. 新增一个 C VM bridge，用 committed `demos/frontend_bootstrap.kub.json` 调用 `dao_core --bootstrap ... temp.ku`。
2. 在 `dao/mcp_server.py` 中让 `ku_eval` 优先走 C VM bridge，失败才 Python fallback。
3. 对 `ku_call` 生成临时代码，例如加载 std 后执行 `目标思(参数...)`，也优先走 C VM。
4. 保留 Python `Thought.registry` 用于列工具/参数 schema，逐步减少执行职责。
5. M3 sqlite builtins 未完成前，经验层工具仍会卡在 C VM 不支持 sqlite；这必须在真正 Phase 5 完成前补齐。

## 下一步路线

最短可继续路径：

1. 完成 M1 原生编译器接入并证明 parity 走 `dao/dao_core.exe`。
2. 继续 M2，把 frame 内所有硬退出和错误打印收口到 `ExecResult`。
3. 做 M3 sqlite builtins，使 `experience.ku` / `task_queue.ku` 可在 C VM 运行。
4. 做 MCP C VM bridge，先切 `ku_eval`，再切 `ku_call`。
5. 最后做 M5 验收：MCP 网关默认 C VM，Python DaoVM 只作为对拍基准或 fallback。

