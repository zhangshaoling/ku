# Ku 自举 Phase 3：compiler.ku 编写指导

> 给正在实现 `dao/std/compiler.ku` 的 agent。目标是把当前半成品从“能加载”推进到“能通过最小编译/执行闭环”。

## 0. 当前状态判断

### 2026-06-11 最新进展

当前 `compiler.ku` 已从“能加载但不可运行”推进到 Phase 3 可验证成果：

- P0 表达式：literal / ref / op / assign / block / call / return 已实现并验证。
- P2 数据结构：list / dict / index 已实现并验证。
- thought 定义可编译输出 `MAKE_FUNCTION + STORE_NAME`。
- 与 Python `DaoVM` 执行结果一致。
- 当前可回滚点：`git checkout bb3cc21`。

下一阶段不要继续盲目扩功能，优先单独修复 Ku runtime `_eval` 的既有 bug。修复后再打通：

- P1 控制流：if / while。
- P2 thought 执行闭环。

### 初始观察记录

当前文件：`D:/Tools/Dao/dao/std/compiler.ku`

已观察到：

- `DaoEnv().load('dao/std/compiler.ku')` 可以成功加载，约 33 个 thought。
- 直接调用 `compile_ast({"type":"literal","value":42})` 会报：`NameError: ku: '或' 未定义`。
- 临时在 Python registry 里补：
  - `Thought.registry['或'] = Thought.registry['or']`
  - `Thought.registry['且'] = Thought.registry['and']`
  后，最小 literal/add 编译能输出合理字节码。

因此它不是废稿，但目前只是“编译器骨架”，还没有打通 Ku runtime、AST、bytecode、VM 四层 ABI。

## 1. 本阶段目标

Phase 3 的正确目标不是一次性覆盖完整 Python `DaoCompiler`，而是先打通闭环：

```text
Ku 源码 / AST
  -> compiler.ku
  -> bytecode dict
  -> DaoVM 执行
  -> 结果与 Python compiler.py 一致
```

建议分三层推进：

1. **P0 表达式编译**：literal / ref / binary op / call / assign / block / return。
2. **P1 基础控制流**：if / while / list / dict / index。
3. **P2 完整特性**：for / try / thought 定义 / lambda / memory op / disassemble。

不要一口气把 Python 版 1400 行全部机械翻译到 Ku。

## 2. 先修接口契约，不要先扩功能

当前最大问题是接口契约没统一。

### 2.1 Ku 原语名称

当前 runtime 里存在英文逻辑原语：`or`、`and`，但 `compiler.ku` 使用了中文：`或`、`且`。

二选一：

- 方案 A：在 `compiler.ku` 里全部改成 `or` / `and`。
- 方案 B：先在 runtime builtin 里正式注入 `或 -> or`、`且 -> and` 中文别名。

为了 Phase 3 稳定，建议优先方案 A：`compiler.ku` 内部先只用已存在原语，减少 runtime 依赖。

### 2.2 bytecode 字段名

`compiler.ku` 当前输出：

```ku
{ "format": "kub", "version": "0.1", "consts": ..., "instrs": ..., "entry": 0 }
```

但 `dao/vm_core.py` 的 `DaoVM.execute()` 读取的是：

```python
bytecode["instructions"]
bytecode["constants"]
```

Python 主编译器也更接近 `instructions/constants` 语义。

建议输出同时兼容：

```ku
{
  "format": "kub",
  "version": "0.1",
  "constants": state["consts"],
  "instructions": state["instrs"],
  "consts": state["consts"],       ;; 可选兼容字段
  "instrs": state["instrs"],       ;; 可选兼容字段
  "entry": 0
}
```

### 2.3 AST 形态

`compiler.ku` 假设 AST 是 dict：

```ku
nd["type"]
nd["value"]
nd["children"]
```

但 Python runtime 的 `Node` 是对象，Python parser 返回的可能不是纯 dict。必须明确 `parse(source)` 是否存在、是否返回 dict AST。

如果 `parse` 还不是 builtin，不要在 `compile_source()` 里假设它存在。先只要求 `compile_ast(ast_dict)` 通过测试；`compile_source()` 可后置。

## 3. 建议修改顺序

### Step 1：让最小 AST 编译不报错

先处理 `或/且`。

验收命令：

```bash
cd /d/Tools/Dao
python - <<'PY'
from dao.runtime import DaoEnv, Thought
import json

env = DaoEnv()
env.load('dao/std/compiler.ku')

r = Thought.registry['compile_ast'].call([{'type':'literal','value':42}])
print(json.dumps(r, ensure_ascii=False))

r2 = Thought.registry['compile_ast'].call([{
    'type':'op','value':'+',
    'children':[{'type':'literal','value':1},{'type':'literal','value':2}]
}])
print(json.dumps(r2, ensure_ascii=False))
PY
```

期望：不报错，输出包含 `LOAD_CONST`、`BINARY_OP`、`RETURN`。

### Step 2：统一 bytecode 字段名

让输出同时含 `instructions/constants`。

验收命令：

```bash
cd /d/Tools/Dao
python - <<'PY'
from dao.runtime import DaoEnv, Thought

env = DaoEnv()
env.load('dao/std/compiler.ku')
bc = Thought.registry['compile_ast'].call([{'type':'literal','value':42}])

assert 'instructions' in bc, bc
assert 'constants' in bc, bc
assert bc['instructions'][0][0] == 'LOAD_CONST'
assert bc['constants'][0] == 42
print('bytecode shape OK')
PY
```

### Step 3：对接 VM 执行 literal/add

用现有 Python VM 执行 compiler.ku 产物。

验收命令：

```bash
cd /d/Tools/Dao
python - <<'PY'
from dao.runtime import DaoEnv, Thought
from dao.compiler import DaoVM

env = DaoEnv()
env.load('dao/std/compiler.ku')

bc = Thought.registry['compile_ast'].call([{'type':'literal','value':42}])
print(DaoVM().execute(bc))

bc2 = Thought.registry['compile_ast'].call([{
    'type':'op','value':'+',
    'children':[{'type':'literal','value':1},{'type':'literal','value':2}]
}])
print(DaoVM().execute(bc2))
PY
```

期望：输出 `42` 和 `3`。

注意：这里用 `dao.compiler.DaoVM`，不要用 `dao.vm_core.DaoVM`，因为 `compiler.py` 内部 VM 更完整。

### Step 4：再做 assign/block/call

测试 AST：

```python
{
  'type': 'block',
  'children': [
    {'type': 'assign', 'value': 'x', 'children': [{'type':'literal','value':1}]},
    {'type': 'op', 'value': '+', 'children': [
      {'type':'ref','value':'x'},
      {'type':'literal','value':2}
    ]}
  ]
}
```

期望执行结果为 `3`。

### Step 5：最后才碰 thought / try / for

`thought`、`try`、`for` 涉及 frame、函数体 bytecode、异常 handler、循环回填，容易把 bug 扩大。P0 没通前不要继续扩这些。

## 4. 代码质量要求

### 不要机械翻译 Python

当前 `compiler.ku` 很像把 Python 编译器结构逐段翻译过去，这是可以作为第一稿，但后续要按 Ku 的真实能力收敛。

重点不是“功能看起来多”，而是：

- 每个 thought 都能被调用；
- 产物能被 VM 接受；
- 最小测试能执行出正确结果；
- 字段名、opcode、AST 结构有文档契约。

### 不要吞异常

`compiler.ku` 内部如果遇到未知节点类型，应该返回/抛出明确错误，不要静默返回 state。

建议加一个 fallback：

```ku
;; 如果所有类型都不匹配，返回错误或发出明确诊断
```

否则错 AST 会被静默编成空程序，后面很难查。

### 控制流回填要用真实 VM 语义验证

`patch_jump` 当前用 `target - addr`。这是否正确取决于 VM 执行 `JUMP` 是 `pc += arg` 还是 `pc = target`。

当前 Python VM 是相对跳转：`pc += arg`，所以理论上对，但必须用 if/while 测试验证。

## 5. 最小验收清单

完成 T1 前，至少通过：

- [ ] `compiler.ku` 可加载。
- [ ] `compile_ast(literal)` 返回合法 bytecode。
- [ ] `compile_ast(add)` 返回合法 bytecode。
- [ ] bytecode 有 `instructions/constants` 字段。
- [ ] Python `DaoVM().execute()` 可执行 literal 得到 `42`。
- [ ] Python `DaoVM().execute()` 可执行 `1+2` 得到 `3`。
- [ ] block/assign/ref 可执行得到正确结果。
- [ ] 与 `dao/compiler.py` 对同一小 AST 的指令序列基本一致。

## 6. 当前已知具体问题

1. `或` 未定义，`且` 未定义。
2. 输出字段 `consts/instrs` 与 VM 预期 `constants/instructions` 不一致。
3. `compile_source()` 假设存在 `parse(source)` builtin，需确认。
4. `compile_node()` 全是连续 `若`，没有 `else-if`/fallback；未知节点会静默成功。
5. `BREAK` / `CONTINUE` 目前直接 emit，和 Python compiler 中循环栈回填语义不完全一致。
6. `compile_thought_core()` 保存/恢复 `consts` 时，函数体常量池和外层常量池关系需要和 Python VM 的 `MAKE_FUNCTION` 实现对齐。

## 7. 下一步修复队列（2026-06-11 复查后更新）

当前不要继续扩 `compiler.ku` 功能。问题已拆成三张小票，按顺序修：

### Ticket A：修 `Thought.call(args=[])` 参数判断

文件：`D:/Tools/Dao/dao/runtime.py`

问题：`Thought.call()` 当前用 `if args` 判断是否传参。空列表 `[]` 会被当成没传参数，导致二参 builtin 被错误调用成 `self.body()`。

位置：`Thought.call()` 里 Python callable 分支，当前逻辑类似：

```python
result = self.body(*args) if args else self.body()
```

目标：改成按 `args is not None` 判断。空列表也是合法参数列表。

验收：`compile_ast(if_true/if_false)` 编译阶段不再报 `missing 2 required positional arguments`。

### Ticket B：修 dict literal parser 动态 value / 多 pair

文件：`D:/Tools/Dao/dao/dao_parser.py`

问题：表达式：

```ku
push([], { "id": loop_id, "breaks": [] })
```

当前会被错误解析成 `push(..., {}, "breaks")`，导致 `_push() takes 2 positional arguments but 3 were given`。

目标：dict literal 应产生：

```text
dict.children = [
  pair("id", [ref("loop_id")]),
  pair("breaks", [list([])])
]
```

验收：`compile_ast(while_min)` 编译阶段不再报 `_push()` 参数数量错误。

### Ticket C：修 `compiler.ku::compile_call` 兼容 call AST 两种形态

文件：`D:/Tools/Dao/dao/std/compiler.ku`

问题：当前 parser/runtime 里普通调用常是：

```text
{"type":"call", "value":"", "children":[ref(func), arg1, ...]}
```

但 `compile_call()` 假设函数名在 `node.value`，所以 `thought 定义 + 调用` 会触发 `IndexError` 或生成错误调用。

目标：`compile_call()` 兼容两种形态：

- `node.value != ""`：函数名在 `value`，参数是 `children`。
- `node.value == ""`：callee 是 `children[0]`，参数是 `children[1:]`。

验收：`thought 定义 + add1(41)` 经 `compiler.ku -> DaoVM.execute()` 得到 `42`。

### 推荐执行顺序

1. 先修 Ticket A，因为它是 runtime 调用协议底层问题。
2. 再修 Ticket B，因为 `while` 卡在 parser 产物错误。
3. 最后修 Ticket C，因为它是 `compiler.ku` 与 AST 契约对齐。
4. 每修一张票只跑对应最小验收，不要顺手扩功能。

## 8. 建议给 agent 的短提示

如果你只需要一句话提示当前 agent：

> 先别扩 `compiler.ku` 功能。按 Ticket A/B/C 分开修：先 `Thought.call(args=[])`，再 dict literal parser，最后 `compile_call` 兼容 `value==""` 的 call AST。每张票只跑自己的最小验收。
