# Dao System Architecture

This document is the canonical map for the Dao runtime line. It separates the
philosophy, the implemented files, and the planned migration path.

North star:

```text
thought = code = memory
```

Mission sentence:

```text
道生万物, 天道教化, 记忆不息, 生生不已
```

## Path Law

```text
dao/      Active runtime line. New core architecture belongs here.
ku/       Compatibility and historical lineage. Do not add new core authority here.
demos/    Deterministic runnable fixtures.
tests/    Test harnesses. Python is allowed here as harness only.
tools/    Build and generation tools.
docs/     Architecture records and migration notes.
```

Rules:

- `.ku` source plus `dao/dao_core.c` is the semantic authority.
- Python may remain as test harness, fixture generator, packaging, and MCP stdio glue.
- New memory, Tiandao, reasoning, and tool semantics belong under `dao/std/`.
- Existing `ku/std/*` modules are compatibility or migration sources unless a task says otherwise.

## Layer Model

```text
User / Agent
    |
    v
Dao / 道
    |
    v
Tiandao / 天道
    |
    +--> Memory / 记忆
    +--> Tool / 工具
    +--> Reasoning / 推理
    +--> Evolution / 演化
    |
    v
C VM / Runtime
```

## Canonical File Map

| Layer | Meaning | Canonical Path | Current Status |
| --- | --- | --- | --- |
| Dao / 道 | Origin of transformations; 81 chapter transform source | `dao/std/daodejing.ku` | Implemented |
| Frontend | Source to bytecode path | `dao/std/lexer.ku`, `dao/std/parser.ku`, `dao/std/compiler.ku` | Implemented |
| Tiandao / 天道 | Meta-rule scheduler: recall, reason, record, promote, decay | `dao/std/tiandao.ku` | Planned canonical file; old draft exists at `ku/std/tiandao.ku` |
| Memory / 记忆 | Six-table memory: Experience, Knowledge, Tool, Concept, Goal, Relation | `dao/std/memory.ku` | Planned canonical file; six-table draft exists at `ku/std/memory.ku` |
| Experience memory | Current executable memory subset used by C VM memory profile | `dao/std/experience.ku` | Implemented |
| Tool promotion | Promote durable memory into callable tools | `dao/std/experience.ku`, later `dao/std/tool.ku` | Partially implemented |
| Reasoning | Dao + memory + tool + optional model orchestration | `dao/std/reasoning.ku` | Planned |
| Learning | Document/data ingestion into knowledge and ability | `dao/std/learning.ku` | Planned |
| World model | Goal -> plan -> execute -> feedback loop | `dao/std/world.ku` | Planned |
| C VM | Native execution substrate | `dao/dao_core.c`, `dao/dao_core.exe` | Implemented; binary is local artifact |
| MCP gateway | Agent tool surface; must not own semantics | `dao/mcp_server.py` | Implemented as Python stdio glue |
| C VM gateway | Test/MCP process boundary to native VM | `dao/c_vm_runtime.py` | Implemented as Python orchestration glue |

## Dao Layer

Dao is the origin layer. It is not above Tiandao; Tiandao is a law that operates
Dao.

Implemented files:

```text
dao/std/daodejing.ku       81 chapter transform system
dao/std/lexer.ku           self-hosted lexer
dao/std/parser.ku          self-hosted parser
dao/std/compiler.ku        self-hosted bytecode compiler
demos/golden_path.ku       direct source demo
```

Direct C VM verification:

```bash
dao/dao_core.exe --bootstrap demos/frontend_bootstrap.kub.json demos/golden_path.ku
```

Expected result:

```json
{"result":42,"memory":"thought = code = memory","code":"thought","thought":"加一"}
```

## Tiandao Layer

Tiandao is the operating law of Dao:

```text
有历史经验 -> 召回
无历史经验 -> 用道推理
推理成功 -> 写经验
经验高信任 -> 晋升工具
工具失败 -> 降级
召回多条 -> 质量排序
任务完成 -> 验证用过的经验
```

Canonical target:

```text
dao/std/tiandao.ku
```

Migration source:

```text
ku/std/tiandao.ku
```

The canonical `dao/std/tiandao.ku` should import active Dao modules only:

```ku
引 "std/daodejing" 别 道
引 "std/memory" 别 忆
引 "std/tool" 别 器
```

It must not depend on `ku/std/*` once migrated.

## Memory Layer

The final memory layer is six tables:

```text
experience     What happened and how it performed
knowledge      Durable facts and learned content
tool_registry  Callable capabilities produced from memory
concept        Abstract ideas and definitions
goal           Active direction and priorities
relation       Links between memories, tools, concepts, goals, and thoughts
```

Canonical target:

```text
dao/std/memory.ku
```

Current executable subset:

```text
dao/std/experience.ku
```

Migration source:

```text
ku/std/memory.ku
```

The migration should preserve `dao/std/experience.ku` behavior first, then extend
it into the six-table memory API rather than replacing it blindly.

## Tool Layer

Tool is not an external add-on. A tool is promoted memory.

Current implemented path:

```text
dao/std/experience.ku
  memory_promote(...)
  memory_promotion_list()
  memory_call(...)

dao/mcp_server.py
  exposes ku_memory_<thought_name> dynamic tools
```

Canonical target:

```text
dao/std/tool.ku
```

`tool.ku` should eventually own promotion, demotion, call stats, and failure
policy while keeping persistent records in `dao/std/memory.ku`.

## Runtime Path

The semantic path is:

```text
.ku source
  -> dao/std/lexer.ku
  -> dao/std/parser.ku
  -> dao/std/compiler.ku
  -> .kub bytecode
  -> dao/dao_core.c
  -> result / memory / tool surface
```

Python is outside this semantic path. When Python appears, it must be described
as one of these roles:

```text
test harness
fixture generator
MCP stdio glue
process launcher for dao_core.exe
```

## Current Truth

Implemented and directly runnable:

```text
dao_core.exe can run demos/golden_path.ku through frontend_bootstrap.kub.json.
dao/std/daodejing.ku exists.
dao/std/experience.ku exists and supports executable experience memory subset.
dao/std/compiler.ku emits bytecode executed by the C VM.
```

Partially implemented:

```text
Memory promotion to dynamic MCP tools.
Experience quality lifecycle.
C VM-backed MCP calls through Python stdio glue.
```

Planned canonical migrations:

```text
ku/std/memory.ku      -> dao/std/memory.ku
ku/std/tiandao.ku     -> dao/std/tiandao.ku
dao/std/experience.ku -> remain as compatibility subset or be folded into memory.ku
```

Planned new modules:

```text
dao/std/tool.ku
dao/std/reasoning.ku
dao/std/learning.ku
dao/std/world.ku
```

## Migration Order

1. Keep `dao_core.c` buildable and verify direct `.ku` execution.
2. Create `dao/std/memory.ku` from the six-table draft in `ku/std/memory.ku`.
3. Preserve the current `dao/std/experience.ku` public API during migration.
4. Create `dao/std/tiandao.ku` as the meta-rule scheduler over `daodejing`, `memory`, and `tool`.
5. Add direct C VM demos for Tiandao and six-table memory.
6. Update test and MCP profiles only after direct `.ku -> C VM` demos pass.

## Success Definition

The architecture is real when this command shape works without Python semantic
fallback:

```bash
dao/dao_core.exe --bootstrap demos/frontend_bootstrap.kub.json \
  dao/std/daodejing.ku \
  dao/std/memory.ku \
  dao/std/tool.ku \
  dao/std/tiandao.ku \
  demos/tiandao_golden_path.ku
```

And the returned value proves:

```text
Dao chapter transform ran.
Tiandao chose recall/reason/tool policy.
Memory was read or written through the six-table layer.
Tool promotion or decay policy was visible in data.
```
