# Ku 自举 Phase 3：compiler.ku 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use compose:subagent (recommended) or compose:execute to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 `dao/std/compiler.ku` 从"能加载"推进到"完整自举闭环"——Ku AST → compiler.ku → bytecode → DaoVM 执行，覆盖全部语法结构。

**Architecture:** compiler.ku 是一个用 Ku 自身实现的字节码编译器。它接收 dict 格式的 AST，输出与 `dao/compiler.py` 兼容的 bytecode dict。当前已完成 P0（表达式编译），需继续推进 P1（控制流）和 P2（完整特性）。P1 被 Ku 运行时 `_eval` 的 `>` 运算符 bug 阻塞，需先修复。

**Tech Stack:** Ku 语言, Python 3.10+, `dao/compiler.py` (DaoVM), `dao/runtime.py` (Ku 运行时)

---

## 文件结构

| 文件 | 职责 |
|------|------|
| `dao/std/compiler.ku` | 主编译器（Ku 实现） |
| `dao/runtime.py` | Ku 运行时 `_eval`（需修复 `>` bug） |
| `dao/compiler.py` | Python 编译器 + DaoVM（参考实现） |
| `tests/test_compiler_ku.py` | compiler.ku 单元测试 |
| `docs/plans/2026-06-11-ku-bootstrap-phase3-compiler-ku.md` | 本计划 |

---

## Task 1: 修复 Ku 运行时 `_eval` 的 `>` 运算符 bug

**Covers:** P1 前置条件

**问题：** `runtime.py` 的 `_eval` 在评估 `op` 类型 AST 节点时，`>` 运算符被当作函数调用而非运算符，导致 `TypeError: missing 2 required positional arguments`。

**Files:**
- Modify: `dao/runtime.py:310-353` (`_eval` 的 `op` 分支)

- [ ] **Step 1: 复现 bug**

```bash
cd D:/Tools/Dao
python -c "
from dao.runtime import DaoEnv, _inject_builtins, Thought, Node
env = DaoEnv()
_inject_builtins(env)
t = Thought('__test__', [], Node.op('>', Node.lit(5), Node.lit(3)))
print(t.call())
"
```

Expected: `TypeError` 或错误结果

- [ ] **Step 2: 检查 `_eval` 的 `op` 分支**

阅读 `runtime.py:310-353`，确认 `>` 操作符是否在 `_eval` 的 `op` case 中被正确处理。当前 `_eval` 对 `op` 节点的处理是：

```python
if t == "op":
    op = node.value
    if op == "and": ...
    if op == "or": ...
    left = self._eval(node.children[0], env)
    right = self._eval(node.children[1], env) if len(node.children) > 1 else None
    if op == "+": return left + right
    ...
```

问题：`>` 不在 `if op == ...` 链中，会 fall through 到底部 `raise ValueError`。但实际报错是 `TypeError` 说明 `>` 被当作函数调用了。

- [ ] **Step 3: 检查 `_eval` 的 `call` 分支**

查看 `runtime.py:355-363`，确认 `call` 节点的处理。如果 `>` 被包装在 `call` 节点中（Python parser 的行为），需要在 `call` 分支中特殊处理运算符前缀调用。

- [ ] **Step 4: 修复 `_eval`**

在 `runtime.py` 的 `_eval` 方法中，确保 `>` 运算符在 `op` 分支中被正确处理（与 `+`, `-`, `*` 等同等对待）。如果问题是 `>` 被 Python parser 包装成 `call` 节点，则在 `call` 分支中添加运算符识别：

```python
if t == "call":
    func = self._eval(node.children[0], env)
    args = [self._eval(c, env) for c in node.children[1:]]
    # 运算符前缀调用
    if isinstance(func, Thought) and func.name in ("+", "-", "*", "/", "%", "==", "!=", "<", ">", "<=", ">="):
        return func.call(args)
    ...
```

- [ ] **Step 5: 验证修复**

```bash
cd D:/Tools/Dao
python -c "
from dao.runtime import DaoEnv, _inject_builtins, Thought, Node
env = DaoEnv()
_inject_builtins(env)
t = Thought('__test__', [], Node.op('>', Node.lit(5), Node.lit(3)))
print(t.call())  # 应输出 True
t2 = Thought('__test2__', [], Node.op('<', Node.lit(1), Node.lit(2)))
print(t2.call())  # 应输出 True
"
```

Expected: `True`, `True`

- [ ] **Step 6: Commit**

```bash
git add dao/runtime.py
git commit -m "fix: ku runtime _eval correctly handles > operator"
```

---

## Task 2: P1 — if/while 控制流编译验证

**Covers:** P1 基础控制流

**Files:**
- Modify: `dao/std/compiler.ku`（如有必要微调）
- Test: 内联 Python 测试脚本

- [ ] **Step 1: 测试 if 编译**

```bash
cd D:/Tools/Dao
python -c "
import sys; sys.path.insert(0, '.')
from dao.runtime import DaoEnv, _inject_builtins, _inject_parser_to_env, Thought
from pathlib import Path
from dao.compiler import DaoVM
from dao.dao_lexer import lex
from dao.dao_parser import parse_tokens as py_parse

env = DaoEnv()
_inject_builtins(env)
_inject_parser_to_env(env)
env.load(str(Path('dao/std/compiler.ku')))

def compile_src(src):
    tokens = lex(src)
    ast = py_parse(tokens)
    return Thought.registry['compile_ast'].call([ast.to_dict()])

# if with else
bc = compile_src('if (x > 0) { 1 } { 0 }')
vm = DaoVM(); vm.env['x'] = 5
assert vm.execute(bc) == 1, 'if true branch failed'
vm2 = DaoVM(); vm2.env['x'] = -1
assert vm2.execute(bc) == 0, 'if false branch failed'
print('if/else OK')
"
```

- [ ] **Step 2: 测试 while 编译**

```bash
cd D:/Tools/Dao
python -c "
import sys; sys.path.insert(0, '.')
from dao.runtime import DaoEnv, _inject_builtins, _inject_parser_to_env, Thought
from pathlib import Path
from dao.compiler import DaoVM
from dao.dao_lexer import lex
from dao.dao_parser import parse_tokens as py_parse

env = DaoEnv()
_inject_builtins(env)
_inject_parser_to_env(env)
env.load(str(Path('dao/std/compiler.ku')))

def compile_src(src):
    tokens = lex(src)
    ast = py_parse(tokens)
    return Thought.registry['compile_ast'].call([ast.to_dict()])

bc = compile_src('i = 0\nsum = 0\nwhile (i < 5) { sum = sum + i\ni = i + 1 }\nsum')
print('while result:', DaoVM().execute(bc))
assert DaoVM().execute(bc) == 10
print('while OK')
"
```

- [ ] **Step 3: 记录结果**

如果 if/while 测试通过，标记 P1 完成。如果仍有问题，在 `docs/plans/` 中记录具体错误。

---

## Task 3: P2 — list/dict/index 编译验证

**Covers:** P2 数据结构

**Files:**
- Test: 内联 Python 测试脚本

- [ ] **Step 1: 测试 list 字面量**

```bash
cd D:/Tools/Dao
python -c "
import sys; sys.path.insert(0, '.')
from dao.runtime import DaoEnv, _inject_builtins, _inject_parser_to_env, Thought
from pathlib import Path
from dao.compiler import DaoVM

env = DaoEnv()
_inject_builtins(env)
_inject_parser_to_env(env)
env.load(str(Path('dao/std/compiler.ku')))

ast = {'type':'list','children':[
    {'type':'literal','value':1},
    {'type':'literal','value':2},
    {'type':'literal','value':3}
]}
bc = Thought.registry['compile_ast'].call([ast])
result = DaoVM().execute(bc)
print('list:', result)
assert result == [1, 2, 3]
print('list OK')
"
```

- [ ] **Step 2: 测试 dict 字面量**

```bash
cd D:/Tools/Dao
python -c "
import sys; sys.path.insert(0, '.')
from dao.runtime import DaoEnv, _inject_builtins, _inject_parser_to_env, Thought
from pathlib import Path
from dao.compiler import DaoVM

env = DaoEnv()
_inject_builtins(env)
_inject_parser_to_env(env)
env.load(str(Path('dao/std/compiler.ku')))

ast = {'type':'dict','children':[
    {'type':'pair','value':'a','children':[{'type':'literal','value':1}]},
    {'type':'pair','value':'b','children':[{'type':'literal','value':2}]}
]}
bc = Thought.registry['compile_ast'].call([ast])
result = DaoVM().execute(bc)
print('dict:', result)
assert result == {'a': 1, 'b': 2}
print('dict OK')
"
```

- [ ] **Step 3: 测试 index 访问**

```bash
cd D:/Tools/Dao
python -c "
import sys; sys.path.insert(0, '.')
from dao.runtime import DaoEnv, _inject_builtins, _inject_parser_to_env, Thought
from pathlib import Path
from dao.compiler import DaoVM

env = DaoEnv()
_inject_builtins(env)
_inject_parser_to_env(env)
env.load(str(Path('dao/std/compiler.ku')))

ast = {'type':'block','children':[
    {'type':'assign','value':'arr','children':[{'type':'list','children':[
        {'type':'literal','value':10},{'type':'literal','value':20},{'type':'literal','value':30}
    ]}]},
    {'type':'index','children':[{'type':'ref','value':'arr'},{'type':'literal','value':1}]}
]}
bc = Thought.registry['compile_ast'].call([ast])
result = DaoVM().execute(bc)
print('index:', result)
assert result == 20
print('index OK')
"
```

---

## Task 4: P2 — thought 定义编译验证

**Covers:** P2 thought 定义

**Files:**
- Test: 内联 Python 测试脚本

- [ ] **Step 1: 测试 thought 定义 + 调用**

```bash
cd D:/Tools/Dao
python -c "
import sys; sys.path.insert(0, '.')
from dao.runtime import DaoEnv, _inject_builtins, _inject_parser_to_env, Thought
from pathlib import Path
from dao.compiler import DaoVM

env = DaoEnv()
_inject_builtins(env)
_inject_parser_to_env(env)
env.load(str(Path('dao/std/compiler.ku')))

ast = {'type':'block','children':[
    {'type':'thought','value':'add','children':['a','b',
        {'type':'block','children':[
            {'type':'return','children':[{'type':'op','value':'+',
                'children':[{'type':'ref','value':'a'},{'type':'ref','value':'b'}]}]}
        ]}
    ]},
    {'type':'call','value':'add','children':[
        {'type':'literal','value':3},{'type':'literal','value':4}
    ]}
]}
bc = Thought.registry['compile_ast'].call([ast])
result = DaoVM().execute(bc)
print('thought call:', result)
assert result == 7
print('thought OK')
"
```

- [ ] **Step 2: 测试递归 thought**

```bash
cd D:/Tools/Dao
python -c "
import sys; sys.path.insert(0, '.')
from dao.runtime import DaoEnv, _inject_builtins, _inject_parser_to_env, Thought
from pathlib import Path
from dao.compiler import DaoVM

env = DaoEnv()
_inject_builtins(env)
_inject_parser_to_env(env)
env.load(str(Path('dao/std/compiler.ku')))

ast = {'type':'block','children':[
    {'type':'thought','value':'fib','children':['n',
        {'type':'block','children':[
            {'type':'if','children':[
                {'type':'op','value':'<=','children':[{'type':'ref','value':'n'},{'type':'literal','value':1}]},
                {'type':'return','children':[{'type':'ref','value':'n'}]},
                {'type':'return','children':[{'type':'op','value':'+','children':[
                    {'type':'call','value':'fib','children':[{'type':'op','value':'-','children':[{'type':'ref','value':'n'},{'type':'literal','value':1}]}]},
                    {'type':'call','value':'fib','children':[{'type':'op','value':'-','children':[{'type':'ref','value':'n'},{'type':'literal','value':2}]}]}
                ]}]}
            ]}
        ]}
    ]},
    {'type':'call','value':'fib','children':[{'type':'literal','value':10}]}
]}
bc = Thought.registry['compile_ast'].call([ast])
result = DaoVM().execute(bc)
print('fib(10):', result)
assert result == 55
print('recursive thought OK')
"
```

---

## Task 5: P2 — for 循环编译验证

**Covers:** P2 for 循环

**Files:**
- Test: 内联 Python 测试脚本

- [ ] **Step 1: 测试 for 循环**

```bash
cd D:/Tools/Dao
python -c "
import sys; sys.path.insert(0, '.')
from dao.runtime import DaoEnv, _inject_builtins, _inject_parser_to_env, Thought
from pathlib import Path
from dao.compiler import DaoVM
from dao.dao_lexer import lex
from dao.dao_parser import parse_tokens as py_parse

env = DaoEnv()
_inject_builtins(env)
_inject_parser_to_env(env)
env.load(str(Path('dao/std/compiler.ku')))

def compile_src(src):
    tokens = lex(src)
    ast = py_parse(tokens)
    return Thought.registry['compile_ast'].call([ast.to_dict()])

bc = compile_src('sum = 0\nfor x in [1,2,3,4,5] { sum = sum + x }\nsum')
result = DaoVM().execute(bc)
print('for sum:', result)
assert result == 15
print('for OK')
"
```

---

## Task 6: 创建正式测试文件

**Covers:** 回归测试

**Files:**
- Create: `tests/test_compiler_ku.py`

- [ ] **Step 1: 创建测试文件**

```python
"""tests/test_compiler_ku.py — compiler.ku 回归测试"""
import sys, os, json
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import pytest
from dao.runtime import DaoEnv, _inject_builtins, _inject_parser_to_env, Thought
from dao.compiler import DaoVM
from pathlib import Path


@pytest.fixture(scope="module")
def env():
    e = DaoEnv()
    _inject_builtins(e)
    _inject_parser_to_env(e)
    e.load(str(Path(__file__).parent.parent / "dao" / "std" / "compiler.ku"))
    return e


def compile_ast(ast_dict):
    return Thought.registry["compile_ast"].call([ast_dict])


def execute(ast_dict):
    bc = compile_ast(ast_dict)
    return DaoVM().execute(bc)


class TestP0Expressions:
    def test_literal(self):
        assert execute({"type": "literal", "value": 42}) == 42

    def test_ref(self):
        vm = DaoVM()
        vm.env["x"] = 10
        bc = compile_ast({"type": "ref", "value": "x"})
        assert vm.execute(bc) == 10

    def test_add(self):
        ast = {"type": "op", "value": "+", "children": [
            {"type": "literal", "value": 1},
            {"type": "literal", "value": 2}
        ]}
        assert execute(ast) == 3

    def test_assign_and_ref(self):
        ast = {"type": "block", "children": [
            {"type": "assign", "value": "x", "children": [{"type": "literal", "value": 42}]},
            {"type": "ref", "value": "x"}
        ]}
        assert execute(ast) == 42

    def test_bytecode_fields(self):
        bc = compile_ast({"type": "literal", "value": 1})
        assert "instructions" in bc
        assert "constants" in bc
        assert bc["instructions"][0][0] == "LOAD_CONST"
        assert bc["constants"][0] == 1


class TestP1ControlFlow:
    def test_if_else(self):
        ast = {"type": "if", "children": [
            {"type": "op", "value": ">", "children": [
                {"type": "literal", "value": 5},
                {"type": "literal", "value": 0}
            ]},
            {"type": "literal", "value": "yes"},
            {"type": "literal", "value": "no"}
        ]}
        assert execute(ast) == "yes"

    def test_if_false_branch(self):
        ast = {"type": "if", "children": [
            {"type": "op", "value": ">", "children": [
                {"type": "literal", "value": -1},
                {"type": "literal", "value": 0}
            ]},
            {"type": "literal", "value": "yes"},
            {"type": "literal", "value": "no"}
        ]}
        assert execute(ast) == "no"

    def test_while(self):
        ast = {"type": "block", "children": [
            {"type": "assign", "value": "i", "children": [{"type": "literal", "value": 0}]},
            {"type": "assign", "value": "s", "children": [{"type": "literal", "value": 0}]},
            {"type": "while", "children": [
                {"type": "op", "value": "<", "children": [
                    {"type": "ref", "value": "i"},
                    {"type": "literal", "value": 5}
                ]},
                {"type": "block", "children": [
                    {"type": "assign", "value": "s", "children": [{"type": "op", "value": "+", "children": [
                        {"type": "ref", "value": "s"}, {"type": "ref", "value": "i"}
                    ]}]},
                    {"type": "assign", "value": "i", "children": [{"type": "op", "value": "+", "children": [
                        {"type": "ref", "value": "i"}, {"type": "literal", "value": 1}
                    ]}]}
                ]}
            ]},
            {"type": "ref", "value": "s"}
        ]}
        assert execute(ast) == 10


class TestDataStructures:
    def test_list(self):
        ast = {"type": "list", "children": [
            {"type": "literal", "value": 1},
            {"type": "literal", "value": 2},
            {"type": "literal", "value": 3}
        ]}
        assert execute(ast) == [1, 2, 3]

    def test_dict(self):
        ast = {"type": "dict", "children": [
            {"type": "pair", "value": "a", "children": [{"type": "literal", "value": 1}]},
            {"type": "pair", "value": "b", "children": [{"type": "literal", "value": 2}]}
        ]}
        assert execute(ast) == {"a": 1, "b": 2}

    def test_index(self):
        ast = {"type": "block", "children": [
            {"type": "assign", "value": "arr", "children": [{"type": "list", "children": [
                {"type": "literal", "value": 10},
                {"type": "literal", "value": 20}
            ]}]},
            {"type": "index", "children": [
                {"type": "ref", "value": "arr"},
                {"type": "literal", "value": 1}
            ]}
        ]}
        assert execute(ast) == 20
```

- [ ] **Step 2: 运行测试**

```bash
cd D:/Tools/Dao
python -m pytest tests/test_compiler_ku.py -v
```

Expected: 所有 P0 测试通过，P1/P2 测试视 Task 1 修复情况

- [ ] **Step 3: Commit**

```bash
git add tests/test_compiler_ku.py
git commit -m "test: add compiler.ku regression tests"
```

---

## Task 7: 反汇编器与文档更新

**Covers:** 工具链完善

**Files:**
- Modify: `dao/std/compiler.ku`（disassemble 函数已存在，验证即可）
- Modify: `README.md`（更新 Phase 3 进度）

- [ ] **Step 1: 验证反汇编器**

```bash
cd D:/Tools/Dao
python -c "
import sys; sys.path.insert(0, '.')
from dao.runtime import DaoEnv, _inject_builtins, _inject_parser_to_env, Thought
from pathlib import Path

env = DaoEnv()
_inject_builtins(env)
_inject_parser_to_env(env)
env.load(str(Path('dao/std/compiler.ku')))

bc = Thought.registry['compile_ast'].call([{'type':'op','value':'+','children':[
    {'type':'literal','value':1},{'type':'literal','value':2}
]}])
print(Thought.registry['disassemble'].call([bc]))
"
```

- [ ] **Step 2: 更新 README.md**

在 `README.md` 的 "The Bootstrap Path" 部分更新 Phase 3 进度：

```markdown
3. **Phase 3** (current) -- `std/compiler.ku` compiles Ku AST to bytecode (P0+P1 done, P2 in progress)
```

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs: update bootstrap phase 3 progress"
```

---

## 依赖关系

```
Task 1 (修复 > bug)
  ↓
Task 2 (if/while 验证) ← 依赖 Task 1
Task 3 (list/dict/index) ← 独立
Task 4 (thought 定义) ← 独立
Task 5 (for 循环) ← 依赖 Task 1
Task 6 (测试文件) ← 依赖 Task 1-5
Task 7 (文档) ← 独立
```

## 并行执行建议

- Task 3、4、7 可以并行（互不依赖）
- Task 1 必须先完成
- Task 2、5 依赖 Task 1
- Task 6 在所有验证通过后执行

## 验收标准

完成本计划后，`compiler.ku` 应能：
1. 编译所有 P0 表达式（literal/ref/op/call/assign/block/return）
2. 编译 P1 控制流（if/while）
3. 编译 P2 数据结构（list/dict/index）
4. 编译 P2 thought 定义和调用
5. 编译 P2 for 循环
6. 通过 `tests/test_compiler_ku.py` 全部测试
7. 与 `dao/compiler.py` 对同一 AST 的输出基本一致
