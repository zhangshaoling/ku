# Ku Migration Guide — Old Chain → New Kernel

> Status: Active
> Last updated: 2026-07-28

## Why Migrate

The old `dao/` package (tree-walking C VM interpreter) is deprecated in favor of the
new high-performance **Register VM** kernel at `bindings/python/dao_kernel/`.

The new kernel provides:

- **10-100x faster** execution via register-based bytecode + AOT compilation
- **Stable C ABI** for FFI with robotics/simulation libraries
- **Deterministic execution** suitable for real-time control loops
- **Binary module format** (.dao) with versioning and verification
- **Host function registration** — expose Python/C functions to Ku code

## Migration Map

### Runtime & Environment

| Old API | New API | Notes |
|---------|---------|-------|
| `dao.DaoEnv()` | `dao_kernel.Runtime()` | New kernel runtime |
| `dao.Thought(name, code)` | `dao_kernel.Thought(name, params, module_bytes)` | Wraps compiled module |
| `dao.parse_daao(source)` | `dao_kernel.Thought.from_source(name, source)` | Compiles .ku source |
| `dao.Node` | Removed | Tree-walking AST no longer used |

### Agent & Planning

| Old API | New API | Notes |
|---------|---------|-------|
| `dao.ReactLoop(goal)` | `dao_kernel.Agent(goal)` | Think-act-observe-replan loop |
| `dao.TaskPlanner` | `dao_kernel.TaskPlanner` | Task decomposition + execution |
| `dao.Task(name, priority)` | `dao_kernel.Task(name, thought, priority)` | Now wraps a Thought |
| `dao.TaskPriority.HIGH` | `dao_kernel.TaskPriority.HIGH` | Same priority levels |

### Memory

| Old API | New API | Notes |
|---------|---------|-------|
| `dao.MemorySystem(dir)` | `dao_kernel.MemorySystem(dir)` | File-backed Thought storage |
| `dao.SelfCorrectionEngine` | `dao_kernel.Agent` | Agent self-corrects via re-plan |

### CLI

| Old Command | New Command |
|------------|------------|
| `python -m dao run file.ku` | `python bindings/python/dao_kernel/cli.py compile file.ku -o out.dao` |
| `python -m dao mcp` | `python dao/mcp_server_kernel.py` |
| `python -m dao react "goal"` | `python bindings/python/dao_kernel/cli.py agent "goal"` |

### MCP Server

| Old | New |
|-----|-----|
| `dao.mcp_server` (old C VM) | `dao.mcp_server_kernel` (new kernel bridge) |
| `dao.mcp_server_v2` | `dao.mcp_server_kernel` |

## Quick Start (New Kernel)

```python
from dao_kernel import Runtime, Thought

# Compile .ku source to Thought
thought = Thought.from_source("add", """
thought main(a: i64, b: i64) -> i64 {
    return a + b
}
""")

# Execute via C ABI
runtime = Runtime()
result = thought.call_i64(runtime, 3, 4)
print(result)  # 7
```

## Host Function Registration (New)

```python
from dao_kernel import Runtime, DaoHostFunction

runtime = Runtime()

def sensor_read() -> float:
    return 42.0

host_fn = DaoHostFunction(
    name="sensor_read",
    arg_types=[],
    ret_type="f64",
    callback=sensor_read,
)
runtime.register_host_function(host_fn)
```

## What Stays on Old Chain (Temporarily)

These capabilities still live in `dao/` and have no new-kernel equivalent yet:

- SQLite/FTS experience memory (`dao/memory_system.ku`)
- Semantic core (`dao/semantic_core.py`)
- HTTP/socket builtins (`test_http.ku`, `test_socket.ku`)
- Dynamic tool registry (`dao/tool_registry.ku`)
- LLM adapter (`dao/runtime.py:LLMAdapter`)

If you depend on these, continue using `dao/` until P16 migration completes.
