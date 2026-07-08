# Codex MCP Tiandao Setup

This note records the local Codex MCP wiring for Dao Tiandao shared memory.

## Goal

All local Codex agents can point at the same Dao MCP server and the same memory
directory:

```text
DAO_DATA_DIR=A:\AGI\AGI gzq\D_Tools_Dao\.dao_data
```

When agents call `ku_tiandao`, writes land in the same `memory.db`.

## Codex Config

Codex reads MCP servers from `C:\Users\32066\.codex\config.toml` or from a
trusted project-scoped `.codex/config.toml`.

Current local server entry:

```toml
[mcp_servers.dao_tiandao]
command = "uv"
args = ["run", "python", "-m", "dao.mcp_server"]
cwd = 'A:\AGI\AGI gzq\D_Tools_Dao'
startup_timeout_sec = 20
tool_timeout_sec = 60

[mcp_servers.dao_tiandao.env]
DAO_DATA_DIR = 'A:\AGI\AGI gzq\D_Tools_Dao\.dao_data'
PYTHONPATH = 'A:\AGI\AGI gzq\D_Tools_Dao'
```

After changing Codex MCP config, start a new thread or restart Codex so the tool
namespace is loaded for the session.

## Verification

Run the shared-memory MCP smoke check from the repo root:

```powershell
uv run python tools/verify_codex_mcp_tiandao.py
```

The script starts `python -m dao.mcp_server` through stdio, initializes MCP,
checks that `ku_tiandao` is listed, calls it with a UTF-8 marker, and reads
`ku_tiandao_stats`.

## Boundaries

- Local low-frequency multi-agent calls are acceptable with SQLite WAL.
- High-concurrency writers need a queue or stronger locking policy.
- Do not expose this stdio server directly to the public network.
- Remote agents should use an authenticated HTTP/SSE wrapper instead of raw
  local stdio.
