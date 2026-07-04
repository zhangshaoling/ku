"""
dao compiler v2.0 — 栈式虚拟机字节码编译器 v2.0
==========================================
将 ku AST 编译成字节码，由 DaoVM 执行。

目标：让 ku 能自举——用 ku 编译自己。

字节码格式：JSON 序列化的指令列表。
每条指令：[opcode, arg] 或 [opcode]（无参数指令）。
"""

import json
import os
import sys
import time
from pathlib import Path
from typing import Any, Optional

# 复用 runtime 的解析器
try:
    from .runtime import parse_道, _parse_body, _parse_expr, Node, Thought, DaoEnv, _inject_builtins, _simplify
except (ImportError, SystemError):
    from .runtime import parse_道, _parse_body, _parse_expr, Node, Thought, DaoEnv, _inject_builtins, _simplify


def _strip_outer_parens(text: str) -> str:
    """剥离外层匹配的括号：(1 + 2) → 1 + 2。
    只有当第一个 ( 和最后一个 ) 配对时才剥离。"""
    text = text.strip()
    if not text.startswith("(") or not text.endswith(")"):
        return text
    # 检查第一个 ( 是否和最后一个 ) 配对
    depth = 0
    for i, ch in enumerate(text):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if depth == 0:
            # 第一个 ( 在位置 i 配对
            if i == len(text) - 1:
                # 完美配对，剥离
                return _strip_outer_parens(text[1:-1].strip())
            else:
                # 不是最后一个字符，不剥离
                return text
    return text


def _extract_non_thought_code(source: str) -> str:
    """提取 thought 定义之外的剩余代码。"""
    lines = source.split("\n")
    result = []
    i = 0
    while i < len(lines):
        stripped = lines[i].strip()
        # 跳过 thought 定义及其 body
        if stripped.startswith("thought ") or stripped.startswith("思 "):
            # 找到对应的 { ... } 块并跳过（深度追踪，depth 回到 0 才算结束）
            depth = 0
            found_open = False
            done = False
            while i < len(lines) and not done:
                for ch in lines[i]:
                    if ch == "{":
                        depth += 1
                        found_open = True
                    elif ch == "}":
                        depth -= 1
                if found_open and depth == 0:
                    done = True
                i += 1
            continue
        # 跳过注释和空行
        if not stripped or stripped.startswith("//") or stripped.startswith(";;"):
            i += 1
            continue
        result.append(lines[i])
        i += 1
    return "\n".join(result)


def _parse_expr_parens(text: str) -> Node:
    """解析表达式，支持括号分组。ku parser 原生不支持括号。

    策略：先做括号感知的运算符分割，再委托给 _parse_expr 处理原子表达式。
    """
    text = text.strip()
    if not text:
        return Node.lit(None)

    # 剥离外层括号
    stripped = _strip_outer_parens(text)
    if stripped != text:
        return _parse_expr_parens(stripped)

    # 检测赋值：name = expr（= 不在运算符列表中，需要特殊处理）
    eq_idx = _find_assignment_equals(text)
    if eq_idx > 0:
        name = text[:eq_idx].strip()
        value = text[eq_idx + 1:].strip()
        if name.isidentifier():
            return Node.assign(name, _parse_expr_parens(value))

    # 括号感知的二元操作符查找
    for op in ["or", "and", "==", "!=", ">=", "<=", ">", "<", "+", "-", "*", "/", "%"]:
        idx = _find_op_parens(text, op)
        if idx > 0:
            left = text[:idx].strip()
            right = text[idx + len(op):].strip()
            if left and right:
                return Node.op(op, _parse_expr_parens(left), _parse_expr_parens(right))

    # 不含顶层操作符，委托给原始 parser
    return _parse_expr(text)


def _find_op_parens(text: str, op: str) -> int:
    """从右到左找操作符位置，括号感知（跳过括号内的内容）。"""
    paren_depth = 0
    in_string = False
    string_char = None
    max_start = len(text) - len(op)
    for i in range(len(text) - 1, -1, -1):
        ch = text[i]
        if ch in ('"', "'"):
            if i > 0 and text[i - 1] == '\\':
                pass
            elif in_string and ch == string_char:
                in_string = False
                string_char = None
            elif not in_string:
                in_string = True
                string_char = ch
        if in_string:
            continue
        if ch in ")]}":
            paren_depth += 1
        elif ch in "([{":
            paren_depth -= 1
        elif i <= max_start and paren_depth == 0 and text[i:i + len(op)] == op:
            if op == "=" and i > 0 and text[i - 1] in ("!", "=", ">", "<"):
                continue
            if op.isalpha():
                if i > 0 and (text[i-1].isalnum() or text[i-1] == '_'):
                    continue
                after = i + len(op)
                if after < len(text) and (text[after].isalnum() or text[after] == '_'):
                    continue
            return i
    return -1


def _find_assignment_equals(text: str) -> int:
    """找到赋值 = 的位置（跳过 ==, !=, >=, <= 和括号内内容）。"""
    paren_depth = 0
    in_string = False
    string_char = None
    for i, ch in enumerate(text):
        if ch in ('"', "'"):
            if i > 0 and text[i - 1] == '\\':
                pass
            elif in_string and ch == string_char:
                in_string = False
            elif not in_string:
                in_string = True
                string_char = ch
        if in_string:
            continue
        if ch in "([{":
            paren_depth += 1
        elif ch in ")]}":
            paren_depth -= 1
        elif ch == '=' and paren_depth == 0:
            # 排除 ==, !=, >=, <=
            if i > 0 and text[i - 1] in ('!', '=', '>', '<'):
                continue
            if i + 1 < len(text) and text[i + 1] == '=':
                continue
            return i
    return -1


# ═══════════════════════════════════════════
#  字节码指令集
# ═══════════════════════════════════════════

# 栈操作
# v2.0 opcodes
REACT_THINK      = "REACT_THINK"
REACT_ACT        = "REACT_ACT"
REACT_OBSERVE    = "REACT_OBSERVE"
REACT_REPLAN     = "REACT_REPLAN"
TASK_CREATE      = "TASK_CREATE"
TASK_COMPLETE    = "TASK_COMPLETE"
MEM_STORE        = "MEM_STORE"
MEM_RECALL       = "MEM_RECALL"
MEM_FORGET       = "MEM_FORGET"
MEM_LINK         = "MEM_LINK"
MEM_SEARCH       = "MEM_SEARCH"
TOOL_REGISTER    = "TOOL_REGISTER"
TOOL_CALL        = "TOOL_CALL"
SELF_REVIEW      = "SELF_REVIEW"
SELF_FIX         = "SELF_FIX"
CONTEXT_COMPRESS = "CONTEXT_COMPRESS"
STREAM_TOKEN     = "STREAM_TOKEN"

LOAD_CONST = "LOAD_CONST"       # arg: 常量值 → push
LOAD_NAME = "LOAD_NAME"         # arg: 名字 → push env[name]
STORE_NAME = "STORE_NAME"       # arg: 名字 → pop, env[name] = val
POP = "POP"                     # → discard TOS
DUP = "DUP"                     # → push TOS copy

# 算术/逻辑
BINARY_OP = "BINARY_OP"         # arg: op → pop right, pop left, push result
UNARY_OP = "UNARY_OP"           # arg: op → pop val, push result

# 控制流
JUMP = "JUMP"                   # arg: offset → pc += offset
JUMP_IF_FALSE = "JUMP_IF_FALSE" # arg: offset → pop, if falsy pc += offset
JUMP_IF_TRUE = "JUMP_IF_TRUE"   # arg: offset → pop, if truthy pc += offset

# 函数
CALL = "CALL"                   # arg: argc → pop argc args + func, push result
RETURN = "RETURN"               # → pop, return from current frame
MAKE_FUNCTION = "MAKE_FUNCTION" # arg: {"params": [...], "body_addr": N} → push Thought

# 数据结构
BUILD_LIST = "BUILD_LIST"       # arg: n → pop n items, push list
BUILD_DICT = "BUILD_DICT"       # arg: n → pop n pairs, push dict

# 属性/索引
GET_ATTR = "GET_ATTR"           # arg: attr_name → pop obj, push obj.attr
SET_ATTR = "SET_ATTR"           # arg: attr_name → pop val, pop obj, obj.attr = val
GET_INDEX = "GET_INDEX"         # → pop index, pop obj, push obj[index]
SET_INDEX = "SET_INDEX"         # → pop val, pop index, pop obj, obj[index] = val

# 异常
TRY_BEGIN = "TRY_BEGIN"         # arg: catch_offset → push handler
TRY_END = "TRY_END"             # → pop handler
RAISE = "RAISE"                 # → pop, raise

# 循环
LOOP_BEGIN = "LOOP_BEGIN"       # arg: loop_id → mark loop start
LOOP_END = "LOOP_END"           # arg: loop_id → mark loop end
BREAK = "BREAK"                 # → break out of current loop

# 栈式 for 循环
GET_ITER = "GET_ITER"           # → pop iterable, push iter(iterable)
FOR_ITER = "FOR_ITER"           # arg: end_offset → push next(iter); if exhausted, jump
STORE_FAST = "STORE_FAST"       # arg: name → pop, store to fast local

# 其他
NOP = "NOP"                     # no-op
IMPORT = "IMPORT"               # arg: module_path → load module thoughts into env

# ── 指令集（用于反汇编显示）──


OPCODES = {
    LOAD_CONST, LOAD_NAME, STORE_NAME, POP, DUP,
    BINARY_OP, UNARY_OP,
    JUMP, JUMP_IF_FALSE, JUMP_IF_TRUE,
    CALL, RETURN, MAKE_FUNCTION,
    BUILD_LIST, BUILD_DICT,
    GET_ATTR, SET_ATTR, GET_INDEX, SET_INDEX,
    TRY_BEGIN, TRY_END, RAISE,
    LOOP_BEGIN, LOOP_END, BREAK,
    GET_ITER, FOR_ITER, STORE_FAST,
    NOP, IMPORT,

    # v2.0 opcodes
    REACT_THINK, REACT_ACT, REACT_OBSERVE, REACT_REPLAN,
    TASK_CREATE, TASK_COMPLETE,
    MEM_STORE, MEM_RECALL, MEM_FORGET, MEM_LINK, MEM_SEARCH,
    TOOL_REGISTER, TOOL_CALL,
    SELF_REVIEW, SELF_FIX,
    CONTEXT_COMPRESS, STREAM_TOKEN,}


# ═══════════════════════════════════════════
#  编译器
# ═══════════════════════════════════════════

class DaoCompiler:
    """
    将 ku AST 编译成栈式虚拟机字节码。

    编译模式：
    - compile(source) → bytecode（从源码编译）
    - compile_node(node) → 指令列表（从 AST 节点编译）
    - compile_file(path) → bytecode（编译文件）
    """

    def __init__(self):
        self.instructions: list[list] = []
        self.constants: list[Any] = []  # 常量池
        self._const_map: dict[int, int] = {}  # id → index
        self._label_counter = 0
        self._loop_stack: list[dict] = []  # 循环上下文栈

    def _emit(self, op: str, arg: Any = None):
        """发射一条指令。"""
        if arg is not None:
            self.instructions.append([op, arg])
        else:
            self.instructions.append([op])

    def _add_const(self, value: Any) -> int:
        """添加常量到常量池，返回索引。"""
        # 对于不可变类型，用 hash 去重
        try:
            if isinstance(value, (int, float, str, bool, type(None))):
                key = hash((type(value), value))
            else:
                key = id(value)
        except TypeError:
            key = id(value)

        if key in self._const_map:
            return self._const_map[key]

        idx = len(self.constants)
        self.constants.append(value)
        self._const_map[key] = idx
        return idx

    def _current_addr(self) -> int:
        """当前指令地址。"""
        return len(self.instructions)

    def _patch_jump(self, addr: int, target: int):
        """回填跳转偏移。"""
        self.instructions[addr][1] = target - addr

    def _last_instruction_produces_value(self) -> bool:
        if not self.instructions:
            return False
        last_op = self.instructions[-1][0]
        return last_op not in (STORE_NAME, STORE_FAST, RETURN, BREAK,
                               LOOP_END, JUMP, JUMP_IF_FALSE, JUMP_IF_TRUE)

    def _discard_last_value(self):
        if self._last_instruction_produces_value():
            self._emit(POP)

    def compile(self, source: str) -> dict:
        """
        编译 .ku 源码为字节码。

        返回：
        {
            "format": "kub",
            "version": "0.1",
            "constants": [...],
            "instructions": [[op, arg], [op], ...],
            "entry": 0,
        }
        """
        self.instructions = []
        self.constants = []
        self._const_map = {}

        # 解析源码为 thought 列表
        thoughts = parse_道(source)

        if thoughts:
            # 有 thought 定义：编译每个 thought
            for t in thoughts:
                self._compile_thought_def(t)

            # 提取 thought 定义之外的剩余代码并编译
            remaining = _extract_non_thought_code(source)
            if remaining.strip():
                ast = _parse_body(remaining)
                self._compile_node(ast)
        else:
            # 没有 thought 定义：当作表达式/语句块直接编译
            source = source.strip()
            is_multi = "\n" in source or "{" in source
            if is_multi:
                ast = _parse_body(source)
            else:
                # 预处理：剥离外层括号（ku parser 不支持括号分组）
                ast = _parse_expr_parens(source)
            self._compile_node(ast)

        # 发射 RETURN
        self._emit(RETURN)

        return {
            "format": "kub",
            "version": "0.1",
            "constants": self.constants,
            "instructions": self.instructions,
            "entry": 0,
        }

    def compile_node(self, node: Node) -> list[list]:
        """
        编译单个 AST 节点为指令列表（不包含函数定义包装）。
        用于 REPL 或表达式求值。
        """
        saved = (list(self.instructions), list(self.constants), dict(self._const_map))
        self.instructions = []
        self.constants = []
        self._const_map = {}
        self._compile_node(node)
        self._emit(RETURN)
        result = {
            "format": "kub",
            "version": "0.1",
            "constants": list(self.constants),
            "instructions": list(self.instructions),
            "entry": 0,
        }
        self.instructions, self.constants, self._const_map = saved
        return result

    def compile_file(self, path: str) -> dict:
        """编译文件。"""
        p = Path(path)
        if not p.exists():
            raise FileNotFoundError(f"找不到文件: {path}")
        source = p.read_text(encoding="utf-8")
        return self.compile(source)

    def compile_ast(self, ast_node: Node) -> dict:
        """
        编译预解析的 AST 节块为字节码。

        用于 ku_lexer + ku_parser 预解析后的 AST，
        绕过旧 parse_道 的限制。
        """
        self.instructions = []
        self.constants = []
        self._const_map = {}

        if ast_node.type == "block":
            for child in ast_node.children:
                if child.type == "thought":
                    self._compile_thought_node(child)
                else:
                    self._compile_node(child)
        elif ast_node.type == "thought":
            self._compile_thought_node(ast_node)
        else:
            self._compile_node(ast_node)

        self._emit(RETURN)

        return {
            "format": "kub",
            "version": "0.1",
            "constants": self.constants,
            "instructions": self.instructions,
            "entry": 0,
        }

    def _compile_thought_def(self, t: Thought):
        """编译一个 thought 定义。"""
        # 跳过 Python 内置 thought
        if callable(t.body):
            return

        # 编译函数体到常量池中的一段指令
        body_instructions = []
        saved = list(self.instructions)
        self.instructions = body_instructions
        self._compile_node(t.body)
        self._emit(RETURN)
        body_instructions = list(self.instructions)
        self.instructions = saved

        # 将函数体指令存为常量
        body_idx = len(self.constants)
        self.constants.append(body_instructions)

        # 发射：创建函数对象并注册到环境
        # MAKE_FUNCTION 参数
        func_info = {
            "params": t.params,
            "body_const_idx": body_idx,
            "doc": t.doc,
            "meta": t.meta,
        }
        func_idx = self._add_const(func_info)
        self._emit(MAKE_FUNCTION, func_idx)
        self._emit(STORE_NAME, t.name)

    def _compile_node(self, node: Node):
        """递归编译 AST 节点。"""
        if node is None:
            self._emit(LOAD_CONST, self._add_const(None))
            return

        t = node.type

        if t == "literal":
            self._emit(LOAD_CONST, self._add_const(node.value))

        elif t == "ref":
            self._emit(LOAD_NAME, node.value)

        elif t == "op":
            self._compile_op(node)

        elif t == "call":
            self._compile_call(node)

        elif t == "block":
            for child in node.children:
                self._compile_node(child)
                # 除了最后一个，其余结果弹出
                # 但如果最后一条指令不产生栈值，不需要 POP
                if child != node.children[-1]:
                    self._discard_last_value()

        elif t == "assign":
            self._compile_assign(node)

        elif t == "if":
            self._compile_if(node)

        elif t == "return":
            if node.children:
                self._compile_node(node.children[0])
            else:
                self._emit(LOAD_CONST, self._add_const(None))
            self._emit(RETURN)

        elif t == "break":
            if not self._loop_stack:
                raise SyntaxError("break 在循环外使用")
            # 记录 break 地址，后续回填跳转目标
            break_addr = self._current_addr()
            self._loop_stack[-1]["breaks"].append(break_addr)
            self._emit(JUMP, 0)  # 占位，回填到 LOOP_END

        elif t == "while":
            self._compile_while(node)

        elif t == "for":
            self._compile_for(node)

        elif t == "try":
            self._compile_try(node)

        elif t == "list":
            self._compile_list(node)

        elif t == "dict":
            self._compile_dict(node)

        elif t == "pair":
            if len(node.children) >= 2:
                self._compile_node(node.children[0])
                self._compile_node(node.children[1])
            else:
                self._emit(LOAD_CONST, self._add_const(node.value))
                self._compile_node(node.children[0])

        elif t == "index":
            self._compile_node(node.children[0])
            self._compile_node(node.children[1])
            self._emit(GET_INDEX)

        elif t == "attr":
            self._compile_node(node.children[0])
            self._emit(GET_ATTR, node.value)

        elif t == "index_assign":
            self._compile_index_assign(node)

        elif t == "thought":
            self._compile_thought_node(node)

        elif t == "return":
            if node.children:
                self._compile_node(node.children[0])
            else:
                self._emit(LOAD_CONST, self._add_const(None))
            self._emit(RETURN)

        elif t == "break":
            self._emit(BREAK)

        elif t == "continue":
            self._emit(JUMP, 0)  # will be patched

        else:
            raise ValueError(f"未知节点类型: {t}")

    def _compile_thought_node(self, node: Node):
        """编译 thought AST 节点（来自 ku_parser）。"""
        name = node.value
        # children: [param_str, param_str, ..., body_block]
        params = []
        body = None
        for c in node.children:
            if isinstance(c, str):
                params.append(c)
            elif isinstance(c, Node) and c.type == "block":
                body = c
            elif isinstance(c, Node):
                # literal param name from dict_to_node
                params.append(c.value)

        # 编译函数体
        body_instructions = []
        saved = list(self.instructions)
        self.instructions = body_instructions
        if body:
            for child in body.children:
                self._compile_node(child)
                if child != body.children[-1]:
                    last_op = self.instructions[-1][0] if self.instructions else None
                    if last_op not in (STORE_NAME, STORE_FAST, RETURN, BREAK):
                        self._emit(POP)
        self._emit(RETURN)
        body_instructions = list(self.instructions)
        self.instructions = saved

        # 存入常量池
        body_idx = len(self.constants)
        self.constants.append(body_instructions)

        func_info = {
            "params": params,
            "body_const_idx": body_idx,
            "doc": "",
            "meta": {},
        }
        func_idx = self._add_const(func_info)
        self._emit(MAKE_FUNCTION, func_idx)
        self._emit(STORE_NAME, name)

    def _compile_op(self, node: Node):
        """编译操作符节点。"""
        op = node.value

        if op in ("not", "非"):
            self._compile_node(node.children[0])
            self._emit(UNARY_OP, "not")
            return

        # 短路求值
        if op == "and":
            self._compile_node(node.children[0])
            self._emit(DUP)
            jump_addr = self._current_addr()
            self._emit(JUMP_IF_FALSE, 0)  # 回填
            self._emit(POP)
            self._compile_node(node.children[1])
            self._patch_jump(jump_addr, self._current_addr())
            return

        if op == "or":
            self._compile_node(node.children[0])
            self._emit(DUP)
            jump_addr = self._current_addr()
            self._emit(JUMP_IF_TRUE, 0)  # 回填
            self._emit(POP)
            self._compile_node(node.children[1])
            self._patch_jump(jump_addr, self._current_addr())
            return

        # 二元操作
        self._compile_node(node.children[0])
        self._compile_node(node.children[1])
        self._emit(BINARY_OP, op)

    _OPERATORS = {"+", "-", "*", "/", "%", "==", "!=", "<", ">", "<=", ">=",
                  "and", "or", "not"}

    def _compile_call(self, node: Node):
        """编译函数调用。支持三种 AST 格式：
        - 旧格式：value=None, children=[func_ref, arg1, arg2, ...]
        - 新格式：value=func_name, children=[arg1, arg2, ...]
        - 运算符调用：value=op_name, children=[arg1, arg2, ...]

        栈布局：func, arg1, arg2, ... → CALL pops args then func.
        """
        if node.value and node.value in self._OPERATORS and len(node.children) >= 2:
            # 运算符调用：and(a, b, c) → 编译为链式二元运算
            self._compile_node(node.children[0])
            for arg in node.children[1:]:
                self._compile_node(arg)
                self._emit(BINARY_OP, node.value)
        elif node.value:
            # 新格式（ku_parser）：函数名在 value 中，children 只有参数
            self._emit(LOAD_NAME, node.value)  # func 先入栈
            for arg in node.children:
                self._compile_node(arg)  # 参数后入栈
            self._emit(CALL, len(node.children))
        else:
            # 旧格式（_parse_expr_parens）：children[0] 是函数引用
            self._compile_node(node.children[0])  # func 先入栈
            for arg in node.children[1:]:
                self._compile_node(arg)  # 参数后入栈
            self._emit(CALL, len(node.children) - 1)

    def _compile_assign(self, node: Node):
        """编译赋值。"""
        self._compile_node(node.children[0])
        self._emit(STORE_NAME, node.value)

    def _compile_if(self, node: Node):
        """编译 if/else。"""
        self._compile_node(node.children[0])  # 条件
        jump_false_addr = self._current_addr()
        self._emit(JUMP_IF_FALSE, 0)  # 回填

        # then 分支
        self._compile_node(node.children[1])

        if len(node.children) > 2:
            # 有 else 分支
            jump_end_addr = self._current_addr()
            self._emit(JUMP, 0)  # 跳过 else
            self._patch_jump(jump_false_addr, self._current_addr())
            self._compile_node(node.children[2])
            self._patch_jump(jump_end_addr, self._current_addr())
        else:
            # 无 else 分支 — 条件为假时需要一个占位值
            jump_end_addr = self._current_addr()
            self._emit(JUMP, 0)  # 跳过 sentinel
            self._patch_jump(jump_false_addr, self._current_addr())
            self._emit(LOAD_CONST, self._add_const(None))
            self._patch_jump(jump_end_addr, self._current_addr())

    def _compile_while(self, node: Node):
        """编译 while 循环。"""
        loop_id = self._label_counter
        self._label_counter += 1

        loop_start = self._current_addr()
        self._emit(LOOP_BEGIN, loop_id)

        # 条件
        self._compile_node(node.children[0])
        jump_end_addr = self._current_addr()
        self._emit(JUMP_IF_FALSE, 0)

        # 循环体
        self._loop_stack.append({"id": loop_id, "breaks": []})
        self._compile_node(node.children[1])
        self._discard_last_value()

        # 跳回循环开始
        self._emit(JUMP, loop_start - self._current_addr())
        loop_end = self._current_addr()
        self._emit(LOOP_END, loop_id)

        # 回填条件跳转
        self._patch_jump(jump_end_addr, loop_end)

        # 回填 break 跳转
        loop_ctx = self._loop_stack.pop()
        for break_addr in loop_ctx["breaks"]:
            self._patch_jump(break_addr, loop_end)

    def _compile_for(self, node: Node):
        """编译 for 循环。"""
        loop_id = self._label_counter
        self._label_counter += 1

        var_name = node.value

        # 编译 iterable
        self._compile_node(node.children[0])
        self._emit(GET_ITER)

        loop_start = self._current_addr()
        self._emit(LOOP_BEGIN, loop_id)

        # FOR_ITER: 如果迭代器耗尽，跳到 end
        for_iter_addr = self._current_addr()
        self._emit(FOR_ITER, 0)  # 回填

        # 存储循环变量
        self._emit(STORE_FAST, var_name)

        # 循环体（push loop context BEFORE compiling body）
        self._loop_stack.append({"id": loop_id, "breaks": []})
        self._compile_node(node.children[1])
        self._discard_last_value()

        # 跳回循环开始
        self._emit(JUMP, loop_start - self._current_addr())
        loop_end = self._current_addr()
        self._emit(LOOP_END, loop_id)

        # 回填 FOR_ITER 跳转
        self._patch_jump(for_iter_addr, loop_end)

        # 回填 break 跳转
        loop_ctx = self._loop_stack.pop()
        for break_addr in loop_ctx["breaks"]:
            self._patch_jump(break_addr, loop_end)

    def _compile_try(self, node: Node):
        """编译 try/catch。"""
        catch_offset_addr = self._current_addr()
        self._emit(TRY_BEGIN, 0)  # 回填

        # try body
        self._compile_node(node.children[0])
        self._emit(TRY_END)

        # 跳过 catch
        jump_end_addr = self._current_addr()
        self._emit(JUMP, 0)

        # catch 分支
        self._patch_jump(catch_offset_addr, self._current_addr())
        if len(node.children) > 1 and node.children[1]:
            # 将错误信息存到变量
            err_var = node.value or "_err"
            self._emit(STORE_NAME, err_var)
            self._compile_node(node.children[1])
        else:
            self._emit(POP)
            self._emit(LOAD_CONST, self._add_const(None))

        self._patch_jump(jump_end_addr, self._current_addr())

    def _compile_list(self, node: Node):
        """编译列表字面量。"""
        for child in node.children:
            self._compile_node(child)
        self._emit(BUILD_LIST, len(node.children))

    def _compile_dict(self, node: Node):
        """编译字典字面量。兼容两种 pair 格式：
        - 旧 parser: pair.children = [key_node, val_node]
        - ku_parser: pair.value = key_str, pair.children = [val_node]
        """
        count = 0
        for child in node.children:
            if child.type == "pair":
                if len(child.children) >= 2:
                    # 旧 parser: children[0]=key, children[1]=value
                    self._compile_node(child.children[0])
                    self._compile_node(child.children[1])
                else:
                    # ku_parser: value=key, children[0]=value
                    self._emit(LOAD_CONST, self._add_const(child.value))
                    self._compile_node(child.children[0])
                count += 1
        self._emit(BUILD_DICT, count)

    def _compile_index_assign(self, node: Node):
        """编译索引赋值。"""
        self._emit(LOAD_NAME, node.value)  # obj
        self._compile_node(node.children[0])  # index
        self._compile_node(node.children[1])  # value
        self._emit(SET_INDEX)


# ═══════════════════════════════════════════
#  反汇编器
# ═══════════════════════════════════════════

def disassemble(bytecode: dict) -> str:
    """
    将字节码反汇编为可读文本。

    用于调试。
    """
    constants = bytecode.get("constants", [])
    instructions = bytecode.get("instructions", [])

    lines = []
    lines.append(f"; kub v{bytecode.get('version', '?')} — {len(constants)} constants, {len(instructions)} instructions")
    lines.append("")

    # 显示常量池
    lines.append("; === CONSTANTS ===")
    for i, c in enumerate(constants):
        if isinstance(c, list) and c and isinstance(c[0], list) and c[0] and isinstance(c[0][0], str) and c[0][0] in OPCODES:
            lines.append(f"  [{i}] <bytecode block: {len(c)} instructions>")
        elif isinstance(c, dict) and "params" in c:
            lines.append(f"  [{i}] <function: params={c['params']}>")
        else:
            lines.append(f"  [{i}] {repr(c)}")
    lines.append("")

    # 显示指令
    lines.append("; === CODE ===")
    for i, instr in enumerate(instructions):
        op = instr[0]
        arg = instr[1] if len(instr) > 1 else None

        if arg is not None:
            if isinstance(arg, int) and op in (JUMP, JUMP_IF_FALSE, JUMP_IF_TRUE, FOR_ITER):
                target = i + arg
                lines.append(f"  {i:4d}  {op:20s}  {arg:+d}  (→ {target})")
            elif isinstance(arg, int) and op == LOAD_CONST:
                val = constants[arg] if arg < len(constants) else "?"
                if isinstance(val, list) and val and isinstance(val[0], list):
                    lines.append(f"  {i:4d}  {op:20s}  {arg}  (<bytecode block>)")
                else:
                    lines.append(f"  {i:4d}  {op:20s}  {arg}  ({repr(val)[:40]})")
            else:
                lines.append(f"  {i:4d}  {op:20s}  {repr(arg)[:50]}")
        else:
            lines.append(f"  {i:4d}  {op}")

    return "\n".join(lines)


# ═══════════════════════════════════════════
#  虚拟机
# ═══════════════════════════════════════════

class Frame:
    """栈帧。"""

    def __init__(self, instructions: list, constants: list, env: dict,
                 parent: Optional['Frame'] = None):
        self.instructions = instructions
        self.constants = constants
        self.env = env
        self.parent = parent
        self.pc = 0  # 程序计数器
        self.stack: list = []  # 操作数栈
        self.try_stack: list = []  # 异常处理栈
        self.loop_stack: list = []  # 循环栈（for break）


class DaoVM:
    """
    ku 虚拟机 — 执行字节码。

    核心循环：
    1. 取指 → 2. 译码 → 3. 执行 → 4. 更新 PC

    支持：
    - Thought 调用（能调用已注册的 Thought）
    - Python 原生函数调用
    - 异常处理
    - 循环控制
    """

    def __init__(self, env: Optional[DaoEnv] = None):
        self.env = env or DaoEnv()
        self._output = []  # 输出缓冲区（用于测试）

        # 确保内置函数已注入
        if "len" not in Thought.registry:
            try:
                from .runtime import _inject_builtins
                _inject_builtins(self.env)
            except ImportError:
                from .runtime import _inject_builtins
                _inject_builtins(self.env)

    def execute(self, bytecode: dict) -> Any:
        """
        执行编译好的字节码。

        返回最后一个表达式的值。
        """
        constants = bytecode["constants"]
        instructions = bytecode["instructions"]

        # 合并 Thought.registry 和已有的 env 变量
        global_env = dict(Thought.registry)
        frame = Frame(instructions, constants, global_env)

        result = self._run_frame(frame)

        # 将 Frame 中的变量同步回 Thought.registry（编译版本优先覆盖）
        for k, v in frame.env.items():
            Thought.registry[k] = v

        return result

    def run(self, source: str) -> Any:
        """编译 + 执行一步到位。"""
        compiler = DaoCompiler()
        bytecode = compiler.compile(source)
        return self.execute(bytecode)

    def run_file(self, path: str) -> Any:
        """编译 + 执行文件。"""
        compiler = DaoCompiler()
        bytecode = compiler.compile_file(path)
        return self.execute(bytecode)

    def _run_frame(self, frame: Frame) -> Any:
        """在栈帧中执行指令。"""
        while frame.pc < len(frame.instructions):
            instr = frame.instructions[frame.pc]
            op = instr[0]
            arg = instr[1] if len(instr) > 1 else None

            # ── 栈操作 ──
            if op == LOAD_CONST:
                if not isinstance(arg, int) and len(instr) > 2 and isinstance(instr[2], int):
                    arg = instr[2]
                frame.stack.append(frame.constants[arg])
                frame.pc += 1

            elif op == LOAD_NAME:
                name = arg
                try:
                    if name in frame.env:
                        frame.stack.append(frame.env[name])
                    elif name in Thought.registry:
                        frame.stack.append(Thought.registry[name])
                    else:
                        raise NameError(f"ku-vm: '{name}' 未定义")
                    frame.pc += 1
                except Exception as e:
                    if not self._handle_frame_exception(frame, e):
                        raise

            elif op == STORE_NAME:
                val = frame.stack.pop()
                name = arg
                # 处理嵌套赋值（meta.version = X）
                if "." in name:
                    parts = name.split(".", 1)
                    obj = frame.env.get(parts[0], {})
                    if isinstance(obj, dict):
                        obj[parts[1]] = val
                    else:
                        setattr(obj, parts[1], val)
                else:
                    frame.env[name] = val
                frame.pc += 1

            elif op == STORE_FAST:
                frame.env[arg] = frame.stack.pop()
                frame.pc += 1

            elif op == POP:
                if frame.stack:
                    frame.stack.pop()
                frame.pc += 1

            elif op == DUP:
                frame.stack.append(frame.stack[-1])
                frame.pc += 1

            # ── 算术/逻辑 ──
            elif op == BINARY_OP:
                right = frame.stack.pop()
                left = frame.stack.pop()
                try:
                    frame.stack.append(self._binary_op(arg, left, right))
                    frame.pc += 1
                except Exception as e:
                    if not self._handle_frame_exception(frame, e):
                        raise

            elif op == UNARY_OP:
                val = frame.stack.pop()
                try:
                    if arg == "not":
                        frame.stack.append(not val)
                    elif arg == "-":
                        frame.stack.append(-val)
                    else:
                        raise ValueError(f"未知一元操作符: {arg}")
                    frame.pc += 1
                except Exception as e:
                    if not self._handle_frame_exception(frame, e):
                        raise

            # ── 控制流 ──
            elif op == JUMP:
                frame.pc += arg

            elif op == JUMP_IF_FALSE:
                val = frame.stack.pop()
                if not val:
                    frame.pc += arg
                else:
                    frame.pc += 1

            elif op == JUMP_IF_TRUE:
                val = frame.stack.pop()
                if val:
                    frame.pc += arg
                else:
                    frame.pc += 1

            # ── 函数 ──
            elif op == MAKE_FUNCTION:
                func_info = frame.constants[arg]
                body_instructions = frame.constants[func_info["body_const_idx"]]
                if "const_offset" in func_info:
                    const_offset = func_info["const_offset"]
                    load_idx = 0
                    normalized = []
                    for body_instr in body_instructions:
                        if body_instr and body_instr[0] == LOAD_CONST:
                            normalized.append([LOAD_CONST, const_offset + load_idx])
                            load_idx += 1
                        else:
                            normalized.append(body_instr)
                    body_instructions = normalized

                # 创建一个 Thought，但 body 用字节码
                thought = Thought(
                    name="__vm_func__",
                    params=func_info.get("params", []),
                    body=None,
                    doc=func_info.get("doc", ""),
                    meta=func_info.get("meta", {}),
                )
                # 用特殊标记标识这是字节码函数
                thought._vm_body = body_instructions
                thought._vm_constants = frame.constants
                frame.stack.append(thought)
                frame.pc += 1

            elif op == CALL:
                argc = arg
                args = [frame.stack.pop() for _ in range(argc)]
                args.reverse()  # 保持参数顺序
                func = frame.stack.pop()
                if isinstance(func, str) and func not in Thought.registry and func:
                    print(f"[DEBUG-VM] CALL string: {repr(func)}, argc={argc}, pc={frame.pc}")
                result = self._call_function(func, args, frame)
                frame.stack.append(result)
                frame.pc += 1

            elif op == RETURN:
                val = frame.stack.pop() if frame.stack else None
                return val

            # ── 数据结构 ──
            elif op == BUILD_LIST:
                items = [frame.stack.pop() for _ in range(arg)]
                items.reverse()
                frame.stack.append(items)
                frame.pc += 1

            elif op == BUILD_DICT:
                d = {}
                for _ in range(arg):
                    val = frame.stack.pop()
                    key = frame.stack.pop()
                    d[key] = val
                frame.stack.append(d)
                frame.pc += 1

            # ── 属性/索引 ──
            elif op == GET_ATTR:
                obj = frame.stack.pop()
                if isinstance(obj, dict):
                    frame.stack.append(obj.get(arg))
                else:
                    frame.stack.append(getattr(obj, arg))
                frame.pc += 1

            elif op == SET_ATTR:
                obj = frame.stack.pop()
                val = frame.stack.pop()
                if isinstance(obj, dict):
                    obj[arg] = val
                else:
                    setattr(obj, arg, val)
                frame.stack.append(val)
                frame.pc += 1

            elif op == GET_INDEX:
                idx = frame.stack.pop()
                obj = frame.stack.pop()
                try:
                    if isinstance(obj, dict):
                        frame.stack.append(obj.get(idx))
                    else:
                        frame.stack.append(obj[idx])
                    frame.pc += 1
                except Exception as e:
                    if not self._handle_frame_exception(frame, e):
                        raise

            elif op == SET_INDEX:
                val = frame.stack.pop()
                idx = frame.stack.pop()
                obj = frame.stack.pop()
                try:
                    if isinstance(obj, (dict, list)):
                        obj[idx] = val
                    else:
                        raise TypeError("object does not support item assignment")
                    frame.stack.append(val)
                    frame.pc += 1
                except Exception as e:
                    if not self._handle_frame_exception(frame, e):
                        raise

            # ── 异常 ──
            elif op == TRY_BEGIN:
                # 记录 catch 地址
                catch_addr = frame.pc + arg
                frame.try_stack.append(catch_addr)
                frame.pc += 1

            elif op == TRY_END:
                frame.try_stack.pop()
                frame.pc += 1

            elif op == RAISE:
                exc = frame.stack.pop()
                if not self._handle_frame_exception(frame, RuntimeError(str(exc))):
                    raise RuntimeError(str(exc))

            # ── 循环 ──
            elif op == LOOP_BEGIN:
                frame.loop_stack.append({"id": arg, "start": frame.pc})
                frame.pc += 1

            elif op == LOOP_END:
                if frame.loop_stack:
                    frame.loop_stack.pop()
                frame.pc += 1

            elif op == BREAK:
                if not frame.loop_stack:
                    raise SyntaxError("break 在循环外使用")
                frame.pc += 1

            elif op == GET_ITER:
                iterable = frame.stack.pop()
                frame.stack.append(iter(iterable))
                frame.pc += 1

            elif op == FOR_ITER:
                try:
                    item = next(frame.stack[-1])  # peek iterator
                    frame.stack.append(item)
                    frame.pc += 1
                except StopIteration:
                    frame.stack.pop()  # remove exhausted iterator
                    frame.pc += arg  # jump to end

            elif op == IMPORT:
                # TODO: 支持字节码中的模块导入
                frame.pc += 1

            elif op == NOP:
                frame.pc += 1

            else:
                raise ValueError(f"未知操作码: {op}")

        return frame.stack[-1] if frame.stack else None

    def _handle_frame_exception(self, frame: Frame, exc: Exception) -> bool:
        """Route an exception to the current frame's catch handler if present."""
        if not frame.try_stack:
            return False
        catch_addr = frame.try_stack.pop()
        frame.stack.append(str(exc))
        frame.pc = catch_addr
        return True

    def _binary_op(self, op: str, left: Any, right: Any) -> Any:
        """执行二元操作。"""
        if op == "+":
            if isinstance(left, str) or isinstance(right, str):
                return str(left) + str(right)
            return left + right
        elif op == "-":
            return left - right
        elif op == "*":
            return left * right
        elif op == "/":
            return left / right
        elif op == "%":
            return left % right
        elif op == "==":
            return left == right
        elif op == "!=":
            return left != right
        elif op == ">":
            return left > right
        elif op == "<":
            return left < right
        elif op == ">=":
            return left >= right
        elif op == "<=":
            return left <= right
        elif op == "and":
            return left and right
        elif op == "or":
            return left or right
        elif op == ".":
            if isinstance(left, dict):
                return left.get(right) if right else left
            return getattr(left, right) if right else left
        else:
            raise ValueError(f"Unknown op: {op}")

    def _call_function(self, func: Any, args: list, caller_frame: Frame) -> Any:
        """调用函数（支持 Thought、Python 函数、VM 字节码函数）。"""
        if isinstance(func, Thought):
            # 检查是否是字节码编译的函数
            if hasattr(func, "_vm_body") and func._vm_body is not None:
                return self._execute_vm_thought(func, args, caller_frame)
            # 调用原始 Thought
            return func.call(args, caller_frame.env)

        if callable(func):
            return func(*args)

        if isinstance(func, str):
            # 尝试从 registry 查找
            if func in Thought.registry:
                return self._call_function(Thought.registry[func], args, caller_frame)
            # 空字符串 + 有参数：可能是旧格式 call("", [func_ref, ...])
            if not func and args:
                first = args[0]
                if callable(first) or isinstance(first, Thought):
                    return self._call_function(first, args[1:], caller_frame)
            # 打印调试信息
            stack_vals = [repr(v)[:30] for v in caller_frame.stack[-5:]] if caller_frame.stack else []
            print(f"[DEBUG] Cannot call string: {repr(func)}, caller_stack={stack_vals}")
            import traceback
            traceback.print_stack(limit=5)

        raise TypeError(f"无法调用: {type(func)}")

    def _execute_vm_thought(self, thought: Thought, args: list,
                            caller_frame: Frame) -> Any:
        """执行字节码编译的 Thought。"""
        # 创建新环境：Thought.registry（Python 侧）+ caller_frame.env（VM 侧，含同批次编译的 thought）
        local_env = dict(Thought.registry)
        local_env.update(caller_frame.env)  # 同一 execute() 中定义的 thought 互相可见
        local_env["meta"] = thought.meta
        local_env["self"] = thought

        # 绑定参数
        for p, a in zip(thought.params, args):
            local_env[p] = a

        thought.meta["executions"] = thought.meta.get("executions", 0) + 1

        # 创建新栈帧
        frame = Frame(
            instructions=thought._vm_body,
            constants=thought._vm_constants,
            env=local_env,
            parent=caller_frame,
        )

        try:
            result = self._run_frame(frame)
            Thought.execution_log.append({
                "time": time.time(),
                "thought": thought.name,
                "args": args,
                "result": _simplify(result),
            })
            return result
        except ReturnSignal as rs:
            result = rs.value
            Thought.execution_log.append({
                "time": time.time(),
                "thought": thought.name,
                "args": args,
                "result": _simplify(result),
                "note": "return",
            })
            return result
        except Exception as e:
            Thought.execution_log.append({
                "time": time.time(),
                "thought": thought.name,
                "args": args,
                "error": str(e),
            })
            # 检查是否有异常处理器
            if caller_frame.try_stack:
                catch_addr = caller_frame.try_stack.pop()
                caller_frame.stack.append(str(e))
                caller_frame.pc = catch_addr
                return self._run_frame(caller_frame)
            raise


# ═══════════════════════════════════════════
#  字节码文件 I/O
# ═══════════════════════════════════════════

def save_bytecode(bytecode: dict, path: str):
    """将字节码保存为 JSON 文件（.kub）。"""
    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)

    # 序列化常量池（处理不可 JSON 序列化的类型）
    def serialize_const(c):
        if isinstance(c, list) and c and isinstance(c[0], list) and c[0] and isinstance(c[0][0], str) and c[0][0] in OPCODES:
            # 字节码块，递归序列化
            return {"__type__": "bytecode_block", "instructions": c}
        if callable(c):
            return {"__type__": "callable", "name": getattr(c, '__name__', str(c))}
        if isinstance(c, dict) and "body_const_idx" in c:
            return {"__type__": "function_info", **c}
        return c

    serializable = {
        "format": bytecode["format"],
        "version": bytecode["version"],
        "constants": [serialize_const(c) for c in bytecode["constants"]],
        "instructions": bytecode["instructions"],
        "entry": bytecode["entry"],
    }

    p.write_text(json.dumps(serializable, ensure_ascii=False, indent=2), encoding="utf-8")


def load_bytecode(path: str) -> dict:
    """从 JSON 文件加载字节码。"""
    p = Path(path)
    if not p.exists():
        raise FileNotFoundError(f"找不到字节码文件: {path}")

    data = json.loads(p.read_text(encoding="utf-8"))

    # 反序列化常量池
    def deserialize_const(c):
        if isinstance(c, dict):
            if c.get("__type__") == "bytecode_block":
                return c["instructions"]
            if c.get("__type__") == "function_info":
                return {k: v for k, v in c.items() if k != "__type__"}
            if c.get("__type__") == "callable":
                return None  # 无法反序列化原生函数
        return c

    data["constants"] = [deserialize_const(c) for c in data["constants"]]
    return data


# ═══════════════════════════════════════════
#  ReturnSignal（复用 runtime 的信号）
# ═══════════════════════════════════════════

class ReturnSignal(Exception):
    """用于实现 return 语句的展开信号。"""
    def __init__(self, value=None):
        self.value = value


# ═══════════════════════════════════════════
#  便捷函数
# ═══════════════════════════════════════════

def compile_道(source: str) -> dict:
    """编译 ku 源码为字节码。"""
    return DaoCompiler().compile(source)


def compile_道_file(path: str) -> dict:
    """编译 ku 文件为字节码。"""
    return DaoCompiler().compile_file(path)


def run_bytecode(bytecode: dict, env: Optional[DaoEnv] = None) -> Any:
    """执行字节码。"""
    vm = DaoVM(env)
    return vm.execute(bytecode)


def run_道_compiled(source: str) -> Any:
    """编译并执行 ku 源码。"""
    bytecode = compile_ku(source)
    return run_bytecode(bytecode)
