# Ku Agent MCP Onboarding

## 当前新内核接入

2026-11-21 的 Python stdio 网关入口为 `python -m dao.mcp_server_kernel`。
当前路径使用 Windows 的 `dao-ku.exe` 和 `libdao_kernel.dll`，先按
[快速开始](../README.md#快速开始) 完成新内核构建，再用 Python 3.11 配置客户端。

将下面的占位路径替换为本机绝对路径。`<KU_REPO>` 是仓库目录，`<KU_PYTHON>` 是
Python 可执行文件，`<KU_MEMORY_DIR>` 是可写的 MCP 记忆目录。

```json
{
  "mcpServers": {
    "ku_kernel": {
      "command": "<KU_PYTHON>",
      "args": ["-m", "dao.mcp_server_kernel"],
      "cwd": "<KU_REPO>",
      "env": {
        "PYTHONPATH": "<KU_REPO>",
        "PYTHONIOENCODING": "utf-8",
        "DAO_KERNEL_DLL_DIRS": "<KU_REPO>/.toolchain/llvm-mingw/bin",
        "DAO_MEMORY_DIR": "<KU_MEMORY_DIR>"
      }
    }
  }
}
```

启动后通过 `tools/list` 检查 `ku_memory_store`、`ku_memory_recall`、
`ku_memory_search` 和 `ku_memory_stats`。下面的工具调用可验证编译、保存和执行。

1. 调用 `ku_memory_store`，参数为 `{"key":"answer","source":"思 main() { 返 42 }"}`。
2. 调用 `ku_memory_recall`，参数为 `{"key":"answer","args":[]}`，结果中的 `result` 为 `42`。
3. 调用 `ku_memory_search`，参数为 `{"query":"answer"}`，应返回该记忆。

此网关通过 Python `MemorySystem` 保存键、元数据和模块文件。原生
`dao-ku --store` / `dao-module` 使用带 identity/version 的 ModuleStore 格式，
两者应选择各自的存储目录。原生格式及保证见 [模块与存储契约](../kernel/MODULE_ABI.md)。

## 历史 C VM 接入记录

下文保留 `dao.mcp_server`、`DAO_DATA_DIR` 和旧工具的历史配置，适用于旧 C VM 迁移验证。

This guide is for another AI agent, MCP client, or local assistant that wants to
connect to Dao/Tiandao executable memory without depending on one user's local
paths.

## What This Server Is

Dao exposes a local MCP server backed by the C VM runtime:

```text
agent / MCP client
  -> python -m dao.mcp_server
  -> C VM profiles
  -> Ku/Dao thoughts
  -> SQLite memory under DAO_DATA_DIR
```

The important rule is:

```text
same DAO_DATA_DIR = shared memory between agents
different DAO_DATA_DIR = isolated memory between agents
```

Do not hardcode another machine's paths. Every install should choose its own
repository path and memory directory.

## Required Paths

Use these placeholders in client configuration:

```text
<DAO_REPO>       Absolute path to this repository root.
<DAO_PYTHON>     Python executable that can import the dao package.
<DAO_DATA_DIR>   Writable directory for persistent memory databases.
```

Recommended defaults:

```text
<DAO_REPO>     = path/to/D_Tools_Dao
<DAO_PYTHON>   = <DAO_REPO>/.venv/Scripts/python.exe       # Windows
<DAO_PYTHON>   = <DAO_REPO>/.venv/bin/python               # macOS/Linux
<DAO_DATA_DIR> = <DAO_REPO>/.dao_data
```

If the agent manager uses `uv`, `<DAO_PYTHON>` can be replaced with:

```text
command = uv
args    = run python -m dao.mcp_server
cwd     = <DAO_REPO>
```

## Generic MCP Config

Use this shape for any MCP client that supports stdio servers:

```json
{
  "mcpServers": {
    "dao_tiandao": {
      "command": "<DAO_PYTHON>",
      "args": ["-m", "dao.mcp_server"],
      "cwd": "<DAO_REPO>",
      "env": {
        "PYTHONPATH": "<DAO_REPO>",
        "DAO_DATA_DIR": "<DAO_DATA_DIR>"
      }
    }
  }
}
```

For Codex-style TOML config:

```toml
[mcp_servers.dao_tiandao]
command = "<DAO_PYTHON>"
args = ["-m", "dao.mcp_server"]
cwd = "<DAO_REPO>"
startup_timeout_sec = 20
tool_timeout_sec = 60

[mcp_servers.dao_tiandao.env]
PYTHONPATH = "<DAO_REPO>"
DAO_DATA_DIR = "<DAO_DATA_DIR>"
```

After changing MCP config, restart the client or start a new session so the tool
namespace is reloaded.

## First Smoke Test

After connection, call these tools in order:

1. `ku_golden_path`
   - Expected: returns `result = 42` and `memory = "thought = code = memory"`.
2. `ku_tiandao`
   - Input: any short question.
   - Expected: returns `action = "reason_and_learn"` and writes an experience.
3. `ku_record_data_memory`
   - Input: a small test fact with tags.
   - Expected: returns an `id` and a `graph` object.
4. `ku_graph_search_memory`
   - Input: the same topic or tag.
   - Expected: returns the graph node created by the write.
5. `ku_graph_expand_memory`
   - Input: the same topic or tag.
   - Expected: returns one-hop neighbors when related memories share tags.

If `ku_record_data_memory` returns a `graph` field, automatic graph memory is
working.

## Core Tool Map

### Runtime

- `ku_eval`: run a Ku source snippet through the C VM gateway.
- `ku_call`: call a loaded Ku thought by name.
- `ku_golden_path`: verify source -> bytecode -> C VM execution.
- `ku_list_thoughts`: list loaded thoughts.

### Tiandao

- `ku_tiandao`: fast meta-rule path; reasons and records experience.
- `ku_tiandao_stats`: memory/tool statistics for the Tiandao store.

### Linear Memory

- `ku_record_experience`: record an attempt, observation, gap, or data item.
- `ku_record_data_memory`: record a structured long-term fact.
- `ku_record_dataset`: register a dataset location and schema.
- `ku_recall_memory`: FTS-backed memory recall.
- `ku_recall_memory_explain`: recall plus match explanation.
- `ku_locate_memory`: return stable `dao://experience/<id>` addresses and routes.

### Graph Memory

- `ku_graph_search_memory`: search graph nodes by title, content, or keyword.
- `ku_graph_expand_memory`: expand matching nodes to one-hop neighbors.
- `ku_graph_memory_stats`: read graph node, edge, and keyword counts.
- `ku_graph_from_experience`: manually rebuild a graph node from an existing
  experience. This is usually unnecessary because new writes auto-index.

### Promotion To Tools

- `ku_suggest_memory_promotions`: find memories worth promoting.
- `ku_promote_memory`: bind a memory to a stable callable thought/tool name.
- `ku_list_memory_promotions`: list active promoted memory tools.
- `ku_call_memory`: call a promoted memory by thought name.
- `ku_memory_<thought_name>`: dynamic MCP tools created from active promotions.

## Memory Model

Dao currently has three complementary memory surfaces:

```text
experience table
  durable record: id, kind, topic, context/input/output, tags

memory_graph_* tables
  graph node: dao://graph/node/<id>
  memory link: dao://experience/<id>
  keyword index: tags/title -> node
  graph edge: shared tags -> one-hop expansion

memory_promotion tables
  promoted memory -> thought_name -> ku_memory_<thought_name>
```

Normal write path:

```text
ku_record_data_memory
  -> experience row
  -> automatic graph node
  -> automatic keyword index
  -> automatic shared-tag edges
  -> optional promotion into callable tool
```

## Sharing And Isolation

Use one shared `<DAO_DATA_DIR>` when several local agents should share memory:

```text
Agent A DAO_DATA_DIR = /shared/dao_memory
Agent B DAO_DATA_DIR = /shared/dao_memory
```

Use separate directories when experiments should not mix:

```text
Agent A DAO_DATA_DIR = /tmp/dao_agent_a
Agent B DAO_DATA_DIR = /tmp/dao_agent_b
```

SQLite is acceptable for local low-frequency multi-agent use. If many agents
write concurrently, put a queue, lock, or service wrapper in front of writes.

## Security Boundaries

- Treat `dao.mcp_server` as a local stdio server, not a public network service.
- Do not expose raw stdio over the internet.
- For remote agents, wrap the server with authentication and an HTTP/SSE bridge.
- Keep `<DAO_DATA_DIR>` writable only by trusted local users/agents.
- Promoted memory tools should be reviewed before relying on them as trusted
  capabilities.

## Troubleshooting

### Tools Do Not Show Up

Restart the MCP client. Tool namespaces are usually loaded at session startup.

### Memory Is Not Shared

Check that all agents use exactly the same `DAO_DATA_DIR` value.

### `ku_record_data_memory` Has No `graph`

The server is probably running an older process. Restart the MCP server/client.

### Calls Are Slow

The current memory profile starts the C VM per call. This is correct but not yet
optimal. The graph and memory calls may take seconds on Windows. A long-lived C
VM worker is the expected future speed upgrade.

### Python Import Fails

Set `PYTHONPATH=<DAO_REPO>` or run the server with `cwd=<DAO_REPO>`.

## Minimal Agent Behavior

An agent connected to Dao should follow this loop:

```text
1. Recall or graph-search relevant memory.
2. Use Tiandao or normal reasoning to act.
3. Record important observations, gaps, datasets, and facts.
4. Let automatic graph memory connect the new record.
5. Promote stable, reusable memories into callable tools when useful.
```

This is the practical meaning of:

```text
data = code = memory = tool = knowledge = capability
```

