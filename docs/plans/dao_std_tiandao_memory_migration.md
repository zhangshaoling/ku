# dao/std Tiandao And Memory Migration Plan

Goal: make `dao/std/` the single canonical runtime path for Dao origin,
Tiandao law, six-table memory, tool promotion, reasoning, and world feedback.

## Current Split

```text
Implemented active runtime:
  dao/std/daodejing.ku
  dao/std/experience.ku
  dao/std/lexer.ku
  dao/std/parser.ku
  dao/std/compiler.ku
  dao/dao_core.c

Compatibility / migration sources:
  ku/std/memory.ku
  ku/std/tiandao.ku
```

Problem:

```text
ku/std/memory.ku has the six-table model.
ku/std/tiandao.ku has the meta-rule draft.
dao/std/experience.ku is what the C VM memory profile actually loads.
```

Therefore, the canonical runtime does not yet have a single clear memory and
Tiandao path.

## Target File Layout

```text
dao/std/
  daodejing.ku     Dao origin: 81 chapter transform source
  memory.ku        Six-table memory API and schema
  experience.ku    Compatibility subset, then thin wrapper or folded module
  tool.ku          Tool promotion, demotion, registry policy
  tiandao.ku       Meta-rule scheduler over Dao, memory, and tool
  reasoning.ku     Dao + memory + tool + optional model orchestration
  learning.ku      Documents/data -> knowledge/concepts/tools
  world.ku         Goals -> plans -> execution -> feedback
```

## Migration Steps

### Step 1: Keep C VM Buildable

Status: done locally after restoring `Frame.parent` in `dao/dao_core.c`.

Verification command:

```bash
dao/dao_core.exe --bootstrap demos/frontend_bootstrap.kub.json demos/golden_path.ku
```

Expected output contains:

```json
{"result":42}
```

### Step 2: Create `dao/std/memory.ku`

Source:

```text
ku/std/memory.ku
```

Target:

```text
dao/std/memory.ku
```

Required public thoughts:

```text
memory_init()
knowledge_add(domain, content, source, tags)
knowledge_search(query, limit)
tool_add(name, description, params, source_exp_id)
tool_call(name)
concept_add(name, definition, examples, domain)
concept_search(query, limit)
goal_add(name, description, priority)
goal_list(status)
relation_add(rel_type, from_type, from_id, to_type, to_id, props)
relation_find(from_type, from_id, rel_type)
memory_recall(table, id, query)
```

Compatibility requirement:

```text
Do not break existing `dao/std/experience.ku` MCP-visible behavior.
```

### Step 3: Create `dao/std/tool.ku`

Source:

```text
dao/std/experience.ku promotion functions
ku/std/memory.ku tool_registry table
```

Target:

```text
dao/std/tool.ku
```

Required public thoughts:

```text
tool_promote(experience_id, thought_name, description)
tool_demote(tool_name, reason)
tool_record_success(tool_name)
tool_record_failure(tool_name, error)
tool_list_active(limit)
tool_quality(tool_name)
```

### Step 4: Create `dao/std/tiandao.ku`

Source:

```text
ku/std/tiandao.ku
```

Target:

```text
dao/std/tiandao.ku
```

Canonical imports:

```ku
引 "std/daodejing" 别 道
引 "std/memory" 别 忆
引 "std/tool" 别 器
```

Required public thoughts:

```text
天道(问题, 上下文)
天道统计()
规则_晋升检查(阈值)
规则_衰减检查(阈值)
```

The seven rules must remain visible in source comments and executable behavior:

```text
1. 有历史经验 -> 优先召回
2. 没有经验 -> 启动道计算推理
3. 推理成功 -> 写经验
4. 经验 trust > 阈值 -> 晋升 Tool
5. Tool 连续失败 -> 降级
6. 召回经验 > N 条 -> 按质量排序
7. 任务完成 -> 验证所有使用过的经验
```

### Step 5: Add Direct C VM Demos

Target demos:

```text
demos/memory_6table_golden_path.ku
demos/tiandao_golden_path.ku
```

Required command shape:

```bash
dao/dao_core.exe --bootstrap demos/frontend_bootstrap.kub.json \
  dao/std/memory.ku \
  demos/memory_6table_golden_path.ku


dao/dao_core.exe --bootstrap demos/frontend_bootstrap.kub.json \
  dao/std/daodejing.ku \
  dao/std/memory.ku \
  dao/std/tool.ku \
  dao/std/tiandao.ku \
  demos/tiandao_golden_path.ku
```

### Step 6: Update Runtime Profiles

File:

```text
dao/c_vm_runtime.py
```

Target profile changes:

```text
memory profile:
  dao/std/memory.ku
  dao/std/experience.ku only if still required for compatibility

tiandao profile:
  dao/std/daodejing.ku
  dao/std/memory.ku
  dao/std/tool.ku
  dao/std/tiandao.ku
```

Python remains only the process launcher and test harness for these profiles.

## Done Definition

This migration is done when direct C VM commands prove all of the following:

```text
1. Six-table memory initializes in DAO_DATA_DIR.
2. Knowledge, concept, goal, relation records can be written and read.
3. Tiandao can choose recall or reason path based on memory state.
4. A high-trust memory can be promoted toward a tool record.
5. Existing experience-memory MCP tests still pass through the C VM path.
```
