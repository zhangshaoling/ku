# AGI 母语语义内核规范

> 版本：v0.1  
> 状态：自举合同草案  
> 原则：Python 是参考实现，不是最终权威。最终权威是本文档、`.ku` 实现、C/原生运行时 ABI 对拍结果。

## 0. 目标

本规范定义 AGI 母语的最小语义内核。它不是语法规范，也不是 Python API 说明。

目标是让同一个 thought 可以在三层中保持语义等价：

1. Python bootstrap/reference 层。
2. Dao/Ku 自举层。
3. C/原生 runtime 层。

任何实现只要宣称支持 AGI 母语内核，必须实现本文定义的数据形状和行为合同。

## 1. 基本原则

1. 代码即数据：源码只是输入形式之一，canonical AST 才是核心表示。
2. 数据即代码：AST、memory、trace、patch 都必须能被 thought 读取和生成。
3. 实现无关：不得把 Python 对象、Python 异常、Python callable 当成规范语义。
4. 可展开：所有短语义必须能展开成 canonical AST。
5. 可审计：任何执行、工具调用、记忆读写、补丁应用都必须能生成 Effect，并进入 Trace。
6. 可回退：Patch 必须能表达反向操作，或者明确声明不可逆。
7. 可自举：核心结构必须能用 `.ku` 表达，Python 只能作为参考和测试器。

## 2. 八个核心对象

| 对象 | 作用 | 最低要求 |
|---|---|---|
| `Node` | AST 节点，代码和数据统一载体 | 可序列化、可递归遍历 |
| `Thought` | 可执行记忆单元 | 有名字、参数、body、meta |
| `Memory` | 可持久状态 | 可写入、读取、更新、序列化 |
| `Env` | 运行上下文 | 管理 thought、memory、tool、trace |
| `Effect` | 一次影响 | 记录类型、目标、载荷、元信息 |
| `Trace` | 审计轨迹 | 按顺序记录 TraceEvent |
| `Patch` | AST 修改 | 支持 add/replace/remove 和 inverse |
| `Tool` | 外部能力入口 | 有名字、风险等级、调用效果记录 |

## 3. 值模型

实现必须至少支持以下值：

```text
空值      null
布尔      true / false
整数      int64 起步
浮点      double 起步
字符串    UTF-8
列表      有序值序列
字典      字符串键到值
节点      Node
thought   Thought 引用或可序列化描述
```

跨实现序列化时，布尔和空值采用 JSON 语义。中文表层关键字如 `真/假/空` 只是语法层映射。

## 4. Node

### 4.1 Canonical 形状

```json
{
  "type": "call",
  "value": "",
  "children": [],
  "meta": {}
}
```

字段：

| 字段 | 类型 | 必需 | 含义 |
|---|---|---|---|
| `type` | 字符串 | 是 | 节点类型 |
| `value` | 任意可序列化值 | 否 | 节点主值，缺省为空字符串或 null |
| `children` | 列表 | 否 | 子节点，缺省空列表 |
| `meta` | 字典 | 否 | 非语义元信息 |

`meta` 不得改变节点求值结果。实现可以忽略未知 `meta` 字段。

### 4.2 基础节点类型

```text
literal      字面量，value 为值
ref          引用，value 为名字
call         调用，children[0] 为 callee，后续为参数
block        顺序执行，children 为语句列表
assign       赋值，value 为目标名，children[0] 为值
op           运算符，value 为运算符名，children 为操作数
if           条件，children = [cond, then, else?]
while        循环，children = [cond, body]
for          遍历，value 为变量名，children = [iterable, body]
return       返回，children[0] 为返回值
break        跳出循环
continue     继续循环
list         列表字面量
dict         字典字面量
pair         字典键值项，value 为键，children[0] 为值
index        索引读取，children = [object, index]
index_assign 索引写入，children = [index_node, value]
try          异常处理，具体形状由运行时 ABI 固定
throw        抛出错误
thought      thought 定义
```

### 4.3 Thought AST

Canonical thought AST：

```json
{
  "type": "thought",
  "value": "fix_bug",
  "children": [
    "target",
    {
      "type": "block",
      "value": "",
      "children": []
    }
  ],
  "meta": {}
}
```

规则：

1. `value` 是 thought 名。
2. `children` 最后一项必须是 body node。
3. `children` 中 body 前面的项必须全部是参数名字符串。
4. body 必须是 Node 形状，通常是 `block`。

## 5. 短语义展开

短语义是给 AI 使用的压缩表达，不是最终执行形态。执行前必须展开为 canonical AST。

### 5.1 字符串步骤

```text
"observe tests.fail"
```

展开为：

```json
{
  "type": "call",
  "value": "",
  "children": [
    {"type": "ref", "value": "observe", "children": []},
    {"type": "literal", "value": "tests.fail", "children": []}
  ]
}
```

规则：

1. 第一个空格前为操作名。
2. 空格后剩余文本作为单个字符串参数。
3. 无空格时为零参数调用。
4. 空字符串非法。

### 5.2 字典步骤

```json
{"patch": {"scope": "minimal"}}
```

展开为：

```text
patch({"scope": "minimal"})
```

规则：

1. 字典步骤必须只有一个键。
2. 键为操作名。
3. 值为 `null` 时表示零参数。
4. 值为列表时表示多参数。
5. 其他值表示单参数。

### 5.3 Thought 展开

```text
thought fix_bug {
  observe tests.fail
  locate cause
  patch minimal
  verify
}
```

底层必须等价于：

```json
{
  "type": "thought",
  "value": "fix_bug",
  "children": [
    {
      "type": "block",
      "children": [
        {"type": "call", "children": [{"type": "ref", "value": "observe"}, {"type": "literal", "value": "tests.fail"}]},
        {"type": "call", "children": [{"type": "ref", "value": "locate"}, {"type": "literal", "value": "cause"}]},
        {"type": "call", "children": [{"type": "ref", "value": "patch"}, {"type": "literal", "value": "minimal"}]},
        {"type": "call", "children": [{"type": "ref", "value": "verify"}]}
      ]
    }
  ]
}
```

## 6. Thought

运行时 Thought 的最低字段：

```json
{
  "name": "fix_bug",
  "params": ["target"],
  "body": {},
  "doc": "",
  "meta": {
    "created": 0,
    "version": 1,
    "executions": 0
  }
}
```

行为：

1. 调用时创建局部环境。
2. 参数按位置绑定。
3. `return` 立即结束 thought。
4. 调用前后必须允许 Trace 记录。
5. self 修改必须通过 Patch 或等价审计机制表达，不能静默改写。

## 7. Memory

Canonical 形状：

```json
{
  "key": "goal",
  "kind": "session",
  "value": {},
  "meta": {},
  "created_at": 0,
  "updated_at": 0
}
```

最低行为：

1. `remember(key, value, kind, meta)` 写入或更新记忆。
2. `recall(key, default)` 读取记忆，不存在时返回 default。
3. 写入产生 `memory.write` Effect。
4. 读取产生 `memory.read` Effect。

`kind` 建议值：

```text
session
long_term
entity
fact
tool
trace
```

## 8. Effect

Canonical 形状：

```json
{
  "kind": "thought.call",
  "target": "thought:fix_bug",
  "payload": {},
  "meta": {}
}
```

字段：

| 字段 | 类型 | 必需 | 含义 |
|---|---|---|---|
| `kind` | 字符串 | 是 | effect 类型 |
| `target` | 字符串 | 是 | 被影响对象 |
| `payload` | 任意值 | 否 | 参数、结果摘要或补丁内容 |
| `meta` | 字典 | 否 | 实现信息 |

标准 kind：

```text
define
thought.call
thought.result
thought.error
memory.write
memory.read
tool.register
tool.call
tool.result
tool.error
patch
runtime.error
```

实现可以增加自定义 kind，但不得改变标准 kind 的语义。

## 9. Trace

Trace 是有序事件列表。

TraceEvent 形状：

```json
{
  "time": 0,
  "thought": "fix_bug",
  "effect": {},
  "node": {},
  "result": {},
  "ok": true
}
```

规则：

1. 事件顺序必须等同于记录顺序。
2. `ok=false` 表示该步骤失败。
3. 失败事件必须保留错误摘要，但不要求跨实现保留同一种异常类型。
4. Trace 必须可序列化。
5. Trace 不得改变程序求值结果。

## 10. Patch

Patch 形状：

```json
{
  "op": "replace",
  "path": ["children", 0, "value"],
  "value": "new",
  "before": "old",
  "reason": "minimal fix"
}
```

字段：

| 字段 | 类型 | 必需 | 含义 |
|---|---|---|---|
| `op` | 字符串 | 是 | `add` / `replace` / `remove` |
| `path` | 列表 | 是 | AST 路径，元素为字符串键或整数索引 |
| `value` | 任意值 | 按 op | 新值 |
| `before` | 任意值 | 建议 | 旧值，用于 inverse |
| `reason` | 字符串 | 否 | 修改原因 |

行为：

1. `replace`：替换 path 指向的值。
2. `add`：向列表插入，或向字典设置键。
3. `remove`：删除 path 指向的值。
4. `apply` 必须是纯操作，不修改输入 AST。
5. `inverse` 必须尽量生成可回退 Patch。
6. 应用 Patch 必须产生 `patch` Effect。

路径例子：

```text
["children", 0]              第一个子节点
["children", 1, "value"]     第二个子节点的 value
["meta", "version"]          meta.version
```

## 11. Tool

Tool 形状：

```json
{
  "name": "observe",
  "description": "",
  "risk": "safe",
  "effect_kind": "tool.call"
}
```

风险等级：

```text
safe
low
medium
high
critical
```

行为：

1. 注册产生 `tool.register` Effect。
2. 调用产生 `tool.call` Effect。
3. 成功产生 `tool.result` 或实现声明的 effect kind。
4. 失败产生 `tool.error`。
5. 高风险工具必须由外层权限系统拦截，语义内核只记录合同。

## 12. Env

Env 是运行上下文，最低能力：

```text
define_thought(name, steps, params)
run_thought(name, args)
remember(key, value, kind, meta)
recall(key, default)
register_tool(tool)
call_tool(name, args)
record(effect)
```

规则：

1. Env 可以有多个实现。
2. Env 必须持有或能访问 thought registry。
3. Env 必须持有 Trace。
4. Env 不应绑定到 Python 类；`.ku` 和 C 都必须能表达同等能力。

## 13. 实现阶段

### P0：规范固定

产物：

```text
docs/AGI母语语义内核规范.md
```

验证：

```text
人工审查字段和行为，不依赖 Python。
```

### P1：Python 参考实现

产物：

```text
dao/semantic_core.py
tests/test_semantic_core.py
```

要求：

1. 只能作为 reference/bootstrap。
2. 不允许新增必须依赖 Python 对象才能表达的语义。
3. 所有输出必须可序列化为 canonical 形状。

### P2：Dao/Ku 自举实现

产物：

```text
dao/std/semantic_core.ku
dao/std/trace.ku
dao/std/patch.ku
```

要求：

1. `.ku` 能生成与 Python 参考实现相同的 canonical AST。
2. `.ku` 能记录 Effect/Trace。
3. `.ku` 能 apply/inverse Patch。

当前状态：

```text
dao/std/semantic_core.ku  已实现第一批 canonical AST 构造与短步骤展开
dao/std/trace.ku          已实现 Effect/Trace 纯数据构造与记录
dao/std/patch.ku          已实现第一批 Patch 构造、apply、inverse、to_effect
dao/std/memory_env.ku     已实现第一批 Env/Memory/Tool registry/Trace 组合
tests/test_semantic_core_vm_compile.py 已验证四个 .ku 语义模块可编译为 bytecode
tests/test_bootstrap_frontend_vm_execute.py 已验证 lexer.ku / parser.ku 入口可经 DaoVM bytecode 执行
tests/test_bootstrap_compiler_vm_execute.py 已验证 compiler.ku 的 compile_ast 入口可经 DaoVM bytecode 执行
tests/test_c_vm_parity.py 已验证 C VM 可执行 compiler.ku 的 compile_ast 最小入口
```

已知自举边界：

```text
当前 parser 在部分函数调用参数位置会把 空 解析为空字符串。
对拍测试中仅对 node/payload/result 这类可选空字段做归一化；
规范层仍以 null/空值为 canonical 表示。

当前 parser 对部分中文复合条件（例如 若 A 且 B 且 C）不够稳定。
.ku 自举内核应优先使用嵌套 若，避免把关键语义写成同一行复合条件。

当前运行时对嵌套索引赋值（例如 env["thoughts"][name] = value）不稳定。
.ku 自举内核应使用：
tmp = env["thoughts"]
tmp[name] = value
env["thoughts"] = tmp
```

### P3：对拍测试

产物：

```text
tests/test_semantic_core_parity.py
```

要求：

1. 同一输入，Python 和 `.ku` 输出完全一致。
2. 测试不检查 Python 内部对象，只检查 canonical dict/list/string/number/bool/null。

### P4：C/原生 runtime ABI

产物：

```text
dao/dao_core.c
```

要求：

1. C 层实现 Node/Value/Effect/Patch 基础 ABI。
2. C 层能执行最小 canonical AST。
3. C 层 effect log 与 `.ku` 层语义一致。

### P5：Python 退场

要求：

1. 核心 demo 不依赖 Python runtime 主逻辑。
2. Python 只保留为 bootstrap/test harness。
3. 文档和测试明确标记 Python reference 非权威。

## 14. 最小验收用例

输入：

```json
{
  "name": "check_state",
  "steps": [
    "observe system.ready",
    "verify"
  ]
}
```

必须展开为 thought AST：

```json
{
  "type": "thought",
  "value": "check_state",
  "children": [
    {
      "type": "block",
      "value": "",
      "children": [
        {
          "type": "call",
          "value": "",
          "children": [
            {"type": "ref", "value": "observe", "children": []},
            {"type": "literal", "value": "system.ready", "children": []}
          ]
        },
        {
          "type": "call",
          "value": "",
          "children": [
            {"type": "ref", "value": "verify", "children": []}
          ]
        }
      ]
    }
  ]
}
```

执行时必须至少产生以下 Trace kind 顺序：

```text
thought.call
tool.call 或 tool
tool.call 或 tool
thought.result
```

如果 `verify` 失败，必须产生：

```text
thought.call
tool.error 或 runtime.error
thought.error
```

## 15. 禁止事项

1. 禁止把 Python callable 作为跨实现语义字段。
2. 禁止让 trace 结果依赖 Python 异常类型名。
3. 禁止让 patch 直接修改输入 AST。
4. 禁止新增只能由 Python runtime 执行的 thought 语义。
5. 禁止把短语义直接执行而不展开为 canonical AST。
6. 禁止继续扩展 Python reference 为新主干；Python 只能补 reference/parity 测试。
7. 禁止为同一语义对象新增平行实现，除非先说明现有实现为什么不能复用。
8. 禁止在测试中重复维护归一化规则；统一放在 `tests/semantic_test_utils.py`。

## 16. 收敛原则

当前已经存在四个 `.ku` 语义模块：

```text
dao/std/semantic_core.ku
dao/std/trace.ku
dao/std/patch.ku
dao/std/memory_env.ku
```

后续优先级：

1. 优先执行对拍和 VM 验证，不再横向新增同类模块。
2. `dao/semantic_core.py` 冻结为 reference/bootstrap，不再承载新语义能力。
3. `memory_env.ku` 中与 `trace.ku` 重合的构造逻辑是临时独立加载策略；当 import/alias 机制稳定后，应收敛为复用 `trace.ku`。
4. 深拷贝逻辑应优先收敛到一个稳定原语或标准库函数，不应在新模块中继续复制。
5. 测试公共加载、调用、归一化逻辑必须复用 `tests/semantic_test_utils.py`。

## 17. 下一步

已实现：

```text
tests/test_semantic_core_vm_execute.py
```

第一批验证已覆盖：

```text
semantic_core.ku 的 语义_构造无参思 可经 VM 执行
trace.ku 核心函数经 VM 执行，结果与解释器路径一致
patch.ku 核心函数经 VM 执行，结果与解释器路径一致
memory_env.ku 环境构造函数经 VM 执行，结果与解释器路径一致
memory_env.ku 的 define/memory/tool 写入函数经 VM 执行，结果与解释器路径一致
C VM 已补齐 keys(dict)、list 拼接、递归 copy 组合 parity
memory_env.ku 的 环境_新 / 环境_定义思 / 环境_记住 / 环境_注册工具 可经 C VM 执行
memory_env.ku 的 环境_忆起并记录 / 环境_取轨迹 / 环境_取思 / 环境_取记忆 可经 C VM 执行
trace.ku 的 轨迹_调用影响 / 轨迹_记录成功 / 轨迹_错误影响 可经 C VM 执行
patch.ku 的 补丁_替换 / 补丁_转影响 / 补丁_应用 / 补丁_反向 可经 C VM 执行
semantic_core.ku 的 语义_节点 / 语义_值转节点 / 语义_构造无参思 可经 C VM 执行
patch.ku 的 add/remove/nested apply 路径可经 C VM 执行
semantic_core.ku / memory_env.ku / patch.ku / trace.ku 的组合 demo 可经 C VM 执行
已生成 `demos/semantic_std_combo.kub.json`，可由 C VM 直接读取执行
lexer.ku 已支持 `//` 行注释，parser.ku 已支持字符串索引赋值，并可解析四个语义标准库源码
`tools/generate_semantic_std_combo_demo.py` 已使用 lexer.ku / parser.ku / compiler.ku 生成 C VM demo bytecode
lexer.ku / parser.ku 已可编译进 DaoVM，并通过 VM 内 `parse_tokens(lex(source))` 对拍语义标准库源码
compiler.ku 已可编译进 DaoVM，并通过 VM 内 `compile_ast(ast)` 生成可执行 bytecode
VM 内 compiler.ku 已可编译 lexer.ku / parser.ku 前端模块，并执行生成的前端 bytecode
VM 内 `lex(source) -> parse_tokens(tokens) -> compile_ast(ast) -> DaoVM.execute(bytecode)` 闭环已对小程序通过
C VM 已可执行 `compiler.ku` 的 `compile_ast(1 + 2 AST)`，输出 bytecode 与 Python DaoVM 对拍一致
已生成 `demos/frontend_compile_demo.kub.json`，可由 C VM 直接读取执行 `.ku` 前端闭环
frontend_compile_demo 已覆盖换行、数字字面量和字符串字面量，C VM parity 达到 108 个真实用例
frontend_compile_demo 已接入 `run_bytecode`，C VM 可在同一进程内执行前端生成的 bytecode 并返回 `ok`
frontend_compile_demo 已升级为 lexer 风格 `make_token` 片段，返回可序列化 token dict
frontend_compile_demo 已升级为 semantic-core 风格 `语义_节点` 片段，经 `.ku` 前端编译后在 C VM 内返回 canonical node dict
已生成 `demos/frontend_bootstrap.kub.json`，C VM 可通过 `--bootstrap <kub> <source.ku>` 读取外部 `.ku` 源码并在 C 侧完成 `lex -> parse_tokens -> compile_ast -> run_bytecode`
C VM 的 `--bootstrap` 已支持多个 `.ku` 源码参数，并可加载真实 `semantic_core.ku` 后执行用户程序调用 `语义_构造无参思`
C VM 的 `--bootstrap` 已可加载真实 `math.ku` / `string.ku`，并执行中文数学/字符串函数；C 字符串索引、切片、len、ord、chr 已切换到 UTF-8 字符语义
```

下一步应继续推进：

```text
继续减少 Python runtime 主逻辑，只保留 compile/test harness
补 `引 "std/..." 别 ...` 模块导入/别名语义，让 `dao/test_core.ku` 不需要手工拼接标准库
将 `run_bytecode` 的错误传播、环境隔离和内存所有权补成正式 runtime ABI
```

完成后，Python runtime 进一步退为测试器，核心语义函数开始走 Dao bytecode。
