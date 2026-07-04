"""
ku — 天书原生母语运行时 v0.1
==============================
不是又一种编程语言。
是「记忆即代码、代码即可变、运行时即一切」。

核心信条：
  1. 一切都是 Node（代码 = 数据）
  2. Thought 是基本单位（= 可执行的记忆）
  3. self 关键字让 thought 能在运行时重写自己
  4. 所有状态可序列化、可恢复、可演化

引导方式：Python bootstrap，最终目标自举。
"""

import json
import os
import re
import sys
import time
import traceback
from pathlib import Path
from typing import Any, Optional

DAO_HOME = Path(os.environ.get("DAO_HOME", Path(__file__).parent))
DAO_DATA_DIR = Path(os.environ.get("DAO_DATA_DIR", DAO_HOME / "data"))


class ReturnSignal(Exception):
    """用于实现 return 语句的展开信号。"""
    def __init__(self, value=None):
        self.value = value


class BreakSignal(Exception):
    """用于实现 break 语句的展开信号。"""
    pass


class ContinueSignal(Exception):
    """用于实现 continue 语句的展开信号。"""
    pass


# ═══════════════════════════════════════════
#  核心类型：Node — 万物皆节点
# ═══════════════════════════════════════════

class Node:
    """ku 的基本单元。表达式、语句、值——全是 Node。"""

    def __init__(self, type: str, value: Any = None, children: Optional[list] = None,
                 meta: Optional[dict] = None):
        self.type = type       # literal, ref, call, block, assign, list, dict, if, op
        self.value = value     # 字面值或名字
        self.children = children or []
        self.meta = meta or {}

    def __repr__(self):
        return f"Node({self.type}, {repr(self.value)}, children={len(self.children)})"

    def to_dict(self):
        """序列化为 JSON 友好格式。"""
        return {
            "type": self.type,
            "value": self.value,
            "children": [c.to_dict() for c in self.children],
            "meta": self.meta,
        }

    @classmethod
    def from_dict(cls, d: dict):
        """从 dict 重建。"""
        return cls(
            type=d["type"],
            value=d.get("value"),
            children=[cls.from_dict(c) for c in d.get("children", [])],
            meta=d.get("meta", {}),
        )

    @classmethod
    def lit(cls, value):
        """创建字面量节点。"""
        return cls("literal", value)

    @classmethod
    def ref(cls, name):
        """创建引用节点。"""
        return cls("ref", name)

    @classmethod
    def op(cls, operator, left, right=None):
        """创建操作符节点。"""
        kids = [left]
        if right is not None:
            kids.append(right)
        return cls("op", operator, kids)

    @classmethod
    def block(cls, statements: list):
        """创建语句块。"""
        return cls("block", children=statements)

    @classmethod
    def call(cls, func_ref, args: Optional[list] = None):
        """创建调用节点。"""
        return cls("call", children=[func_ref] + (args or []))

    @classmethod
    def assign(cls, name, value_node):
        """创建赋值节点。"""
        return cls("assign", name, [value_node])

    @classmethod
    def if_(cls, cond, then_branch, else_branch=None):
        """创建条件节点。"""
        kids = [cond, then_branch]
        if else_branch:
            kids.append(else_branch)
        return cls("if", children=kids)

    @classmethod
    def list_(cls, items: list):
        """创建列表字面量。"""
        return cls("list", children=items)

    @classmethod
    def dict_(cls, pairs: dict):
        """创建字典字面量。"""
        kids = []
        for k, v in pairs.items():
            kids.append(cls("pair", children=[cls.lit(k), v]))
        return cls("dict", children=kids)

    @classmethod
    def while_(cls, cond, body):
        """创建 while 循环节点。"""
        return cls("while", children=[cond, body])

    @classmethod
    def for_(cls, var, iterable, body):
        """创建 for 循环节点。"""
        return cls("for", var, [iterable, body])

    @classmethod
    def try_(cls, body, handler, err_var="_err"):
        """创建 try/except 节点。"""
        return cls("try", err_var, [body, handler])


# ═══════════════════════════════════════════
#  Thought — 可执行的记忆
# ═══════════════════════════════════════════

class Thought:
    """
    ku 的基本可执行单位。

    每个 thought 是：
    - 一段记忆（可序列化、可回溯）
    - 一段代码（可执行）
    - 一个实体（有 meta 属性，可自省）
    - 可变的（self 关键字允许在运行时重写自身）
    """

    registry: dict[str, 'Thought'] = {}  # 全局注册表
    execution_log: list[dict] = []       # 全局执行日志
    load_conflicts: list[dict] = []      # thought 同名加载冲突记录

    def __init__(self, name: str, params: list[str], body: Node,
                 doc: str = "", meta: Optional[dict] = None):
        self.name = name
        self.params = params
        self.body = body
        self.doc = doc
        self.meta = meta or {
            "created": time.time(),
            "executions": 0,
            "version": 1,
        }
        # 注册到全局 registry（不覆盖已有 builtin）
        if name not in Thought.registry or not Thought.registry[name].meta.get("builtin"):
            Thought.registry[name] = self

    def __repr__(self):
        return f"Thought({self.name}, params={self.params}, execs={self.meta.get('executions', 0)})"

    def call(self, args: Optional[list] = None, env: Optional[dict] = None):
        """执行这个 thought。"""
        self.meta["executions"] = self.meta.get("executions", 0) + 1
        local_env = dict(Thought.registry)

        # 闭包环境（lambda 捕获的外层变量）
        if hasattr(self, '_closure_env'):
            local_env.update(self._closure_env)

        # meta 对象：thought 可以读写自己的 meta
        local_env["meta"] = self.meta
        # self 关键字：指向这个 thought 本身，允许自修改
        local_env["self"] = self

        if env:
            local_env.update(env)

        # 绑定参数
        if self.params and args:
            for p, a in zip(self.params, args):
                local_env[p] = a

        # Python 原生函数体（内置 thought 的引导桥接）
        if callable(self.body):
            call_args = args if args is not None else []
            result = self.body(*call_args)
            Thought.execution_log.append({
                "time": time.time(),
                "thought": self.name,
                "args": args,
                "result": _simplify(result),
            })
            return result

        # 字节码编译的函数 — 用 VM 执行
        if hasattr(self, "_vm_body") and self._vm_body is not None:
            try:
                try:
                    from .compiler import KuVM, Frame as VMFrame
                except ImportError:
                    from compiler import KuVM, Frame as VMFrame
                vm = KuVM()
                # Mock caller frame with required attributes
                mock_frame = VMFrame([], [], local_env)
                result = vm._execute_vm_thought(self, args or [], mock_frame)
                Thought.execution_log.append({
                    "time": time.time(),
                    "thought": self.name,
                    "args": args,
                    "result": _simplify(result),
                })
                return result
            except Exception:
                pass  # fallback to interpreter

        try:
            result = self._eval(self.body, local_env)
            Thought.execution_log.append({
                "time": time.time(),
                "thought": self.name,
                "args": args,
                "result": _simplify(result),
            })
            return result
        except ReturnSignal as rs:
            result = rs.value
            Thought.execution_log.append({
                "time": time.time(),
                "thought": self.name,
                "args": args,
                "result": _simplify(result),
                "note": "return",
            })
            return result
        except Exception as e:
            Thought.execution_log.append({
                "time": time.time(),
                "thought": self.name,
                "args": args,
                "error": str(e),
            })
            raise

    def clone(self, new_name: Optional[str] = None):
        """克隆 thought，用于演化分支。"""
        name = new_name or f"{self.name}_v{self.meta.get('version', 1) + 1}"
        t = Thought(name, list(self.params),
                    _clone_node(self.body),
                    doc=self.doc,
                    meta=dict(self.meta))
        t.meta["version"] = self.meta.get("version", 1) + 1
        t.meta["cloned_from"] = self.name
        t.meta["created"] = time.time()
        return t

    # ── 求值器 ──

    def _eval(self, node: Node, env: dict) -> Any:
        """AST 求值器。"""
        if node is None:
            return None

        t = node.type

        if t == "literal":
            return node.value

        if t == "ref":
            name = node.value
            # 处理点号属性访问：_obj.attr
            if "." in name:
                parts = name.split(".", 1)
                obj = self._eval(Node("ref", parts[0]), env)
                return getattr(obj, parts[1])
            if name in env:
                val = env[name]
                if isinstance(val, Thought):
                    return val
                return val
            raise NameError(f"ku: '{name}' 未定义")

        if t == "op":
            op = node.value
            # 短路求值：and/or 先算左边，再决定是否算右边
            if op == "and":
                left = self._eval(node.children[0], env)
                if not left:
                    return left
                if len(node.children) > 1:
                    return self._eval(node.children[1], env)
                return left
            if op == "or":
                left = self._eval(node.children[0], env)
                if left:
                    return left
                if len(node.children) > 1:
                    return self._eval(node.children[1], env)
                return left
            left = self._eval(node.children[0], env)
            right = self._eval(node.children[1], env) if len(node.children) > 1 else None
            if op == "+":
                return left + right
            elif op == "-":
                return left - right if right is not None else -left
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
            elif op == "not":
                return not left
            elif op == ".":
                if isinstance(left, dict):
                    return left.get(right) if right else left
                return getattr(left, right) if right else left
            raise ValueError(f"未知操作符: {op}")

        if t == "call":
            # 运算符前缀调用脱糖：>(a, b) → 等价于 op(>, a, b)
            _op_names = ("+", "-", "*", "/", "%", "==", "!=", ">", "<", ">=", "<=")
            if (len(node.children) >= 1
                and isinstance(node.children[0], dict)
                and node.children[0].get("type") == "ref"
                and node.children[0].get("value") in _op_names):
                op_name = node.children[0]["value"]
                left = self._eval(node.children[1], env) if len(node.children) > 1 else None
                right = self._eval(node.children[2], env) if len(node.children) > 2 else None
                return self._eval(Node.op(op_name, Node.lit(left), Node.lit(right)), env)
            # 第一个 child 是被调用者，剩下是参数
            func = self._eval(node.children[0], env)
            args = [self._eval(c, env) for c in node.children[1:]]
            # 运算符 Thought 调用脱糖
            if isinstance(func, Thought) and func.name in _op_names:
                if len(args) >= 2:
                    return self._eval(Node.op(func.name, Node.lit(args[0]), Node.lit(args[1])), env)
                elif len(args) == 1:
                    return self._eval(Node.op(func.name, Node.lit(args[0]), Node.lit(None)), env)
                else:
                    return None  # 空 args：AST 生成 bug，安全跳过
            if isinstance(func, Thought):
                return func.call(args, env)
            elif callable(func):
                return func(*args)
            raise TypeError(f"无法调用 {type(func)}")

        if t == "block":
            result = None
            for child in node.children:
                result = self._eval(child, env)
            return result

        if t == "assign":
            name = node.value
            value = self._eval(node.children[0], env)
            # 处理 meta.version = X 这种嵌套赋值
            if "." in name:
                parts = name.split(".", 1)
                obj = env.get(parts[0], {})
                if isinstance(obj, dict):
                    obj[parts[1]] = value
                else:
                    setattr(obj, parts[1], value)
            else:
                env[name] = value
            return value

        if t == "if":
            cond = self._eval(node.children[0], env)
            if cond:
                return self._eval(node.children[1], env)
            elif len(node.children) > 2:
                return self._eval(node.children[2], env)
            return None

        if t == "return":
            if node.children:
                raise ReturnSignal(self._eval(node.children[0], env))
            raise ReturnSignal(None)

        if t == "break":
            raise BreakSignal()

        if t == "continue":
            raise ContinueSignal()

        if t == "throw":
            if node.children:
                val = self._eval(node.children[0], env)
                if isinstance(val, Exception):
                    raise val
                raise RuntimeError(str(val))
            raise RuntimeError("抛: 无值")

        if t == "try":
            # node.children: [try_body, catch_var_ref, catch_body, finally_body]
            try_body = node.children[0]
            catch_var = node.children[1].value if hasattr(node.children[1], 'value') else ""
            catch_body = node.children[2]
            finally_body = node.children[3]
            # 是否有 catch？有 catch 时 finally 用 catch 后的 env
            has_catch = bool(catch_body and catch_body.type != "literal")
            try:
                result = self._eval(try_body, env)
                if has_catch:
                    # 成功时 finally 用原始 env
                    pass
                if finally_body and finally_body.type != "literal":
                    self._eval(finally_body, env)
                return result
            except Exception as e:
                if has_catch:
                    catch_env = dict(env)
                    if catch_var:
                        catch_env[catch_var] = str(e)
                    result = self._eval(catch_body, catch_env)
                    if finally_body and finally_body.type != "literal":
                        # finally 用 catch 修改后的 env
                        final_env = dict(env)
                        final_env.update(catch_env)
                        self._eval(finally_body, final_env)
                        # finally 的修改写回外层 env
                        env.update(final_env)
                    return result
                if finally_body and finally_body.type != "literal":
                    self._eval(finally_body, env)
                return None

        if t == "while":
            result = None
            try:
                while self._eval(node.children[0], env):
                    try:
                        result = self._eval(node.children[1], env)
                    except BreakSignal:
                        break
                    except ContinueSignal:
                        continue
            except ReturnSignal:
                raise  # 让 return 信号继续向上
            return result

        if t == "for":
            var = node.value
            iterable = self._eval(node.children[0], env)
            body = node.children[1]
            result = None
            try:
                for item in iterable:
                    env[var] = item
                    try:
                        result = self._eval(body, env)
                    except BreakSignal:
                        break
                    except ContinueSignal:
                        continue
            except ReturnSignal:
                raise  # 让 return 信号继续向上
            return result

        if t == "try":
            try:
                return self._eval(node.children[0], env)
            except Exception as e:
                # children: [try_body, ref(catch_var), catch_body]
                if len(node.children) > 2:
                    catch_var = node.children[1].value  # ref 的 value = 变量名
                    env[catch_var] = str(e)
                    return self._eval(node.children[2], env)
                return None

        if t == "list":
            return [self._eval(c, env) for c in node.children]

        if t == "dict":
            result = {}
            for child in node.children:
                if child.type == "pair":
                    key = child.value
                    val = self._eval(child.children[0], env)
                    result[key] = val
            return result

        if t == "attr":
            # 属性访问：obj.name
            obj = self._eval(node.children[0], env)
            attr = node.value
            if isinstance(obj, dict):
                return obj.get(attr)
            return getattr(obj, attr, None)

        if t == "index":
            # 索引操作：list[index] 或 dict[key]
            obj = self._eval(node.children[0], env)
            idx = self._eval(node.children[1], env)
            if isinstance(obj, dict):
                return obj.get(idx)
            return obj[idx]

        if t == "index_assign":
            # 索引赋值：children[0] = index_node, children[1] = value
            # index_node: children[0] = obj, children[1] = key
            index_node = node.children[0]
            obj = self._eval(index_node.children[0], env)
            key = self._eval(index_node.children[1], env)
            val = self._eval(node.children[1], env)
            if isinstance(obj, dict):
                obj[key] = val
                return val
            if isinstance(obj, list):
                obj[key] = val
                return val
            raise TypeError(f"无法索引赋值: {type(obj)}")

        if t == "lambda":
            # children: [body_block, param_lit1, param_lit2, ...]
            params = [c.value for c in node.children[1:]]
            body = node.children[0]
            anon = Thought("__lambda_" + str(id(node)), params, body)
            # 捕获当前环境（闭包）
            anon._closure_env = dict(env)
            return anon

        # ── import: 引 "path" 别 alias ──
        if t == "import":
            # node.value = alias, node.children[0] = path literal
            path_node = node.children[0]
            path = path_node.value if path_node.type == "literal" else str(path_node.value)
            alias = node.value  # 别名

            # 解析路径：支持相对路径（相对于当前文件）和 std/ 简写
            if path.startswith("std/") or path.startswith("std\\"):
                # std 简写：相对于 DAO_DIR/std/
                dao_dir = getattr(self, '_dao_dir', os.path.dirname(os.path.abspath(__file__)))
                resolved = os.path.join(dao_dir, path)
            elif os.path.isabs(path):
                resolved = path
            else:
                # 相对路径：相对于当前正在加载的文件
                base = getattr(self, '_current_file_dir', os.getcwd())
                resolved = os.path.normpath(os.path.join(base, path))

            # 确保 .ku 后缀
            if not resolved.endswith(".ku"):
                resolved += ".ku"

            # 模块缓存
            if not hasattr(self, '_module_cache'):
                self._module_cache = {}
            if resolved in self._module_cache:
                module_dict = self._module_cache[resolved]
            else:
                # 创建子环境加载模块
                if os.path.exists(resolved):
                    saved_dir = getattr(self, '_current_file_dir', os.getcwd())
                    self._current_file_dir = os.path.dirname(resolved)
                    try:
                        sub_env = DaoEnv()
                        sub_env._dao_dir = getattr(self, '_dao_dir', os.path.dirname(os.path.abspath(__file__)))
                        sub_env.load(resolved)
                        # 导出模块中所有 thought 为 dict
                        module_dict = {}
                        for name, thought in Thought.registry.items():
                            if isinstance(thought, Thought) and isinstance(name, str):
                                module_dict[name] = thought
                    finally:
                        self._current_file_dir = saved_dir
                else:
                    module_dict = {}
                self._module_cache[resolved] = module_dict

            # 绑定到当前环境
            if alias:
                env[alias] = module_dict
            return module_dict

        raise ValueError(f"未知节点类型: {t}")

    # ── Python 转译 ──

    def to_python(self, indent: int = 0) -> str:
        """将 thought 转译为 Python 代码。"""
        sp = "  " * indent
        params_str = ", ".join(self.params)
        body_str = self._node_to_python(self.body, indent + 1)
        return f'{sp}def {self.name}({params_str}):\n{body_str}'

    def _node_to_python(self, node: Node, indent: int = 0) -> str:
        """AST → Python 源码。"""
        sp = "  " * indent
        t = node.type

        if t == "literal":
            return f"{sp}{repr(node.value)}"

        if t == "ref":
            name = node.value
            if name == "self":
                return f"{sp}self_thought"
            return f"{sp}{name}"

        if t == "op":
            op = node.value
            left = self._node_to_python(node.children[0], 0).strip()
            right = self._node_to_python(node.children[1], 0).strip() if len(node.children) > 1 else ""
            py_op = {"and": " and ", "or": " or ", "not": "not ",
                     ".": ".", "%": " % "}.get(op, f" {op} ")
            if op == "not":
                return f"{sp}{py_op}{left}"
            return f"{sp}({left}{py_op}{right})"

        if t == "call":
            func = self._node_to_python(node.children[0], 0).strip()
            args = ", ".join(self._node_to_python(c, 0).strip() for c in node.children[1:])
            return f"{sp}{func}({args})"

        if t == "block":
            lines = []
            for child in node.children:
                lines.append(self._node_to_python(child, indent))
            return "\n".join(lines)

        if t == "assign":
            val = self._node_to_python(node.children[0], 0).strip()
            return f"{sp}{node.value} = {val}"

        if t == "if":
            cond = self._node_to_python(node.children[0], 0).strip()
            then_sp = self._node_to_python(node.children[1], indent + 1)
            result = f"{sp}if {cond}:\n{then_sp}"
            if len(node.children) > 2:
                else_sp = self._node_to_python(node.children[2], indent + 1)
                result += f"\n{sp}else:\n{else_sp}"
            return result

        if t == "while":
            cond = self._node_to_python(node.children[0], 0).strip()
            body = self._node_to_python(node.children[1], indent + 1)
            return f"{sp}while {cond}:\n{body}"

        if t == "for":
            var = node.value
            iterable = self._node_to_python(node.children[0], 0).strip()
            body = self._node_to_python(node.children[1], indent + 1)
            return f"{sp}for {var} in {iterable}:\n{body}"

        if t == "return":
            if node.children:
                val = self._node_to_python(node.children[0], 0).strip()
                return f"{sp}return {val}"
            return f"{sp}return None"

        if t == "break":
            return f"{sp}break"

        if t == "continue":
            return f"{sp}continue"

        if t == "try":
            body = self._node_to_python(node.children[0], indent + 1)
            result = f"{sp}try:\n{body}"
            if len(node.children) > 1 and node.children[1]:
                err_var = node.value
                handler = self._node_to_python(node.children[1], indent + 1)
                result += f"\n{sp}except Exception as {err_var}:\n{handler}"
            return result

        if t == "list":
            items = ", ".join(self._node_to_python(c, 0).strip() for c in node.children)
            return f"{sp}[{items}]"

        return f"{sp}# (untranslatable: {t})"


# ═══════════════════════════════════════════
#  ku 解析器
# ═══════════════════════════════════════════

def parse_道(source: str) -> list[Thought]:
    """
    解析 .ku 源码文件（token-stream 驱动 + Python parser 解析 body）。

    用 ku_lexer 做 tokenization → token-stream 匹配 thought 声明和 {/} →
    用 Python ku_parser 解析 body AST（作为 bootstrap 阶段的可靠后备）。
    """
    from .dao_lexer import lex as ku_lex
    from .dao_parser import parse_tokens_as_nodes

    tokens = ku_lex(source)
    thoughts = []
    pos = 0
    total = len(tokens)

    while pos < total:
        tok = tokens[pos]
        pos += 1

        if tok["type"] != "keyword" or tok["value"] not in ("thought", "思"):
            continue

        # 函数名
        if pos >= total:
            break
        name = tokens[pos]["value"]
        pos += 1

        # 参数列表
        params = []
        if pos < total and tokens[pos]["value"] == "(":
            pos += 1
            while pos < total and tokens[pos]["value"] != ")":
                if tokens[pos]["type"] == "name":
                    params.append(tokens[pos]["value"])
                pos += 1
            if pos < total:
                pos += 1

        # 找 body 的 {
        body_start = -1
        while pos < total:
            if tokens[pos]["type"] == "punct" and tokens[pos]["value"] == "{":
                body_start = pos
                break
            pos += 1
        if body_start == -1:
            continue

        # token-stream brace matching
        body_end = -1
        depth = 0
        while pos < total:
            t = tokens[pos]
            if t["type"] == "punct" and t["value"] == "{":
                depth += 1
            elif t["type"] == "punct" and t["value"] == "}":
                depth -= 1
                if depth == 0:
                    body_end = pos
                    break
            pos += 1
        if body_end == -1:
            continue

        # 提取 body text（用 token pos 从原始源码切片）
        body_text = source[tokens[body_start]["pos"] + 1:tokens[body_end]["pos"]].strip()

        # 提取文档注释
        doc = ""
        if body_text.startswith('"') or body_text.startswith("'"):
            quote = body_text[0]
            end_q = body_text.find(quote, 1)
            if end_q > 0:
                after_str = body_text[end_q + 1:].strip()
                if after_str and not any(
                    after_str.startswith(op) for op in ["+", "-", "*", "/",
                                                        "==", "!=", ">=", "<=",
                                                        ">", "<", "and", "or"]
                ):
                    doc = body_text[1:end_q]
                    body_text = after_str

        # 用 Python parser 解析 body（bootstrap 阶段的可靠后备）
        if body_text:
            body_tokens = ku_lex(body_text)
            body_tokens.append({"type": "eof", "value": "", "line": 0, "col": 0, "pos": 0})
            body_ast = parse_tokens_as_nodes(body_tokens)
        else:
            body_ast = Node.lit(None)

        thoughts.append(Thought(name, params, body_ast, doc=doc))

    return thoughts


def _parse_brace_block(text: str, start: int) -> tuple[str, str]:
    """从 start 处的 { 开始匹配到对应的 }，返回 (body, rest)。"""
    if start >= len(text) or text[start] != "{":
        return "", text
    brace_count = 0
    end = start
    for j in range(start, len(text)):
        if text[j] == "{":
            brace_count += 1
        elif text[j] == "}":
            brace_count -= 1
            if brace_count == 0:
                end = j
                break
    body = text[start + 1:end].strip()
    rest = text[end + 1:].strip()
    return body, rest


def _parse_body(text: str) -> Node:
    """解析 thought/block 体（支持多语句）。"""
    stmts = _split_body_statements(text)
    if len(stmts) > 1:
        return Node.block([_parse_expr(s) for s in stmts])
    return _parse_expr(stmts[0]) if stmts else Node.lit(None)


def _parse_dot_expression(text: str) -> Node:
    """解析含点的完整表达式，支持链式多点+索引：a.b[0].c → a['b'][0]['c']。
    从左到右构建：遇到 . 则 dot op，遇到 [n] 则 index op。"""
    segments = _split_dot_smart(text)
    node = _parse_expr(segments[0])
    for seg in segments[1:]:
        if "[" in seg:
            brk = seg.index("[")
            key_part = seg[:brk]
            rest = seg[brk:]
            if key_part:
                node = Node.op(".", node, Node.lit(key_part))
            while rest:
                end = _find_bracket_end(rest)
                index_text = rest[1:end].strip()
                node = Node("index", children=[node, _parse_expr(index_text)])
                rest = rest[end + 1:].strip()
        else:
            node = Node.op(".", node, Node.lit(seg))
    return node


def _split_dot_smart(text: str) -> list[str]:
    """按点分割（跳过括号内的点），用于 a.b[0].c → ['a', 'b[0]', 'c']。"""
    parts, depth, cur = [], 0, ""
    for ch in text:
        if ch in "[({":
            depth += 1; cur += ch
        elif ch in "])}":
            depth -= 1; cur += ch
        elif ch == "." and depth == 0:
            parts.append(cur); cur = ""
        else:
            cur += ch
    if cur: parts.append(cur)
    return parts or [""]


def _find_bracket_end(text: str) -> int:
    """找到匹配 ']' 的位置（处理嵌套）。"""
    depth = 0
    for i, ch in enumerate(text):
        if ch == "[":
            depth += 1
        elif ch == "]":
            depth -= 1
            if depth == 0:
                return i
    return len(text) - 1


def _parse_expr(text: str) -> Node:
    """
    极简表达式解析器。
    支持：字面量、引用、二元操作、函数调用、赋值、if。
    """
    text = text.strip()
    if not text:
        return Node.lit(None)

    # 字面量 — 支持字符串后跟操作符
    if text.startswith('"') or text.startswith("'"):
        end = text.find(text[0], 1)
        if end > 0:
            val = text[1:end]
            rest = text[end + 1:].strip()
            if rest:
                for op in ["==", "!=", ">=", "<=", "+", "-", "*", "/", ">", "<", "and", "or"]:
                    if rest.startswith(op):
                        right = rest[len(op):].strip()
                        return Node.op(op, Node.lit(val), _parse_expr(right))
            return Node.lit(val)

    if text.startswith("true"):
        return Node.lit(True)
    if text.startswith("false"):
        return Node.lit(False)
    if text == "null" or text == "none":
        return Node.lit(None)

    # 数字
    try:
        if "." in text:
            return Node.lit(float(text))
        return Node.lit(int(text))
    except ValueError:
        pass

    # return
    if text == "return":
        return Node("return")
    if text.startswith("return ") or text.startswith("return("):
        if text.startswith("return "):
            expr_text = text[7:].strip()
        else:
            expr_text = text[7:-1].strip()  # return(...) -> ...
        if expr_text:
            return Node("return", children=[_parse_expr(expr_text)])
        return Node("return")

    # break
    if text == "break":
        return Node("break")

    # continue
    if text == "continue":
        return Node("continue")

    def _find_brace_outside_parens(text):
        """找到括号和字符串外的第一个 { 位置。"""
        depth = 0
        in_str = False
        str_ch = ""
        i = 0
        while i < len(text):
            ch = text[i]
            if in_str:
                if ch == "\\" and i + 1 < len(text):
                    i += 2
                    continue
                if ch == str_ch:
                    in_str = False
            else:
                if ch in ('"', "'"):
                    in_str = True
                    str_ch = ch
                elif ch == "(":
                    depth += 1
                elif ch == ")":
                    depth -= 1
                elif ch == "{" and depth == 0:
                    return i
            i += 1
        return -1

    # if 表达式
    if text.startswith("if "):
        # if cond { then } else { else }
        rest = text[3:]
        # 找到括号外的第一个 {（跳过条件中的字符串字面量）
        then_idx = _find_brace_outside_parens(rest)
        if then_idx > 0:
            cond_text = rest[:then_idx].strip()
            then_body, rest2 = _parse_brace_block(rest, then_idx)
            else_body = ""
            rest2 = rest2.strip()
            if rest2.startswith("else"):
                else_text = rest2[4:].strip()
                if else_text.startswith("{"):
                    else_body, _ = _parse_brace_block(else_text, 0)
            elif rest2.startswith("{"):
                # 隐式 else：} { 不需要 else 关键字
                else_body, _ = _parse_brace_block(rest2, 0)
            return Node.if_(
                _parse_expr(cond_text),
                _parse_body(then_body),
                _parse_body(else_body) if else_body else None,
            )

    # while 循环
    if text.startswith("while "):
        rest = text[6:]
        brace_idx = _find_brace_outside_parens(rest)
        if brace_idx > 0:
            cond_text = rest[:brace_idx].strip()
            body, _ = _parse_brace_block(rest, brace_idx)
            return Node.while_(_parse_expr(cond_text), _parse_body(body))

    # for 循环
    if text.startswith("for "):
        rest = text[4:]
        in_idx = rest.find(" in ")
        if in_idx > 0:
            var = rest[:in_idx].strip()
            after_in = rest[in_idx + 4:].strip()
            brace_idx = _find_brace_outside_parens(after_in)
            if brace_idx > 0:
                iter_text = after_in[:brace_idx].strip()
                body, _ = _parse_brace_block(after_in, brace_idx)
                return Node.for_(var, _parse_expr(iter_text), _parse_body(body))

    # try/except
    if text.startswith("try "):
        rest = text[3:].strip()
        if rest.startswith("{"):
            body, rest2 = _parse_brace_block(rest, 0)
            rest2 = rest2.strip()
            if rest2.startswith("catch"):
                catch_text = rest2[5:].strip()
                err_var = "_err"
                if catch_text.startswith("("):
                    paren_end = catch_text.find(")")
                    if paren_end > 0:
                        err_var = catch_text[1:paren_end].strip()
                        catch_text = catch_text[paren_end + 1:].strip()
                if catch_text.startswith("{"):
                    handler, _ = _parse_brace_block(catch_text, 0)
                    return Node.try_(_parse_body(body), _parse_body(handler), err_var)
            return Node.try_(_parse_body(body), Node.lit(None))

    # 列表 [a, b, c]
    if text.startswith("[") and text.endswith("]"):
        inner = text[1:-1].strip()
        if not inner:
            return Node.list_([])
        items = _split_comma(inner)
        return Node.list_([_parse_expr(item.strip()) for item in items])

    # dict {a: b, c: d}
    if text.startswith("{") and text.endswith("}"):
        inner = text[1:-1].strip()
        if not inner:
            return Node.dict_({})
        pairs = _split_comma(inner)
        d = {}
        for pair in pairs:
            if ":" in pair:
                k, v = pair.split(":", 1)
                key = k.strip()
                # 去掉字符串键的引号
                if (key.startswith('"') and key.endswith('"')) or (key.startswith("'") and key.endswith("'")):
                    key = key[1:-1]
                d[key] = _parse_expr(v.strip())
        return Node.dict_(d)

    # 一元 not
    if text.startswith("not "):
        return Node.op("not", _parse_expr(text[4:].strip()))

    # 赋值: name = expr（只有合法的变量名 += 才算赋值）
    assign_match = re.match(r'^([a-zA-Z_][a-zA-Z0-9_]*(\.[a-zA-Z_][a-zA-Z0-9_]*)*)\s*=(?!=)', text)
    if assign_match:
        name = assign_match.group(1)
        value_text = text[assign_match.end():].strip()
        if value_text:
            return Node.assign(name, _parse_expr(value_text))

    # 索引赋值: var[key] = expr
    if "=" in text and not text.startswith("if") and not text.startswith("for"):
        eq_idx = _find_operator(text, "=")
        if eq_idx > 0:
            lhs = text[:eq_idx].strip()
            if lhs.endswith("]") and "[" in lhs:
                idx_open = lhs.index("[")
                obj_name = lhs[:idx_open].strip()
                if obj_name.isidentifier():
                    idx_text = lhs[idx_open + 1:-1].strip()
                    val_text = text[eq_idx + 1:].strip()
                    if idx_text and val_text:
                        return Node("index_assign", obj_name,
                                     children=[_parse_expr(idx_text), _parse_expr(val_text)])

    # lambda: name -> expr  或  (a, b) -> expr
    lambda_idx = _find_operator(text, "->")
    if lambda_idx > 0:
        param_text = text[:lambda_idx].strip()
        body_text = text[lambda_idx + 2:].strip()
        if param_text and body_text:
            # 解析参数
            if param_text.startswith("(") and param_text.endswith(")"):
                inner = param_text[1:-1].strip()
                params = [p.strip() for p in _split_comma(inner)]
            else:
                params = [param_text]
            body = _parse_expr(body_text)
            # lambda 节点：children = [body_block, param_lit1, param_lit2, ...]
            children = [Node.block([Node("return", "", [body])])] + [Node.lit(p) for p in params]
            return Node("lambda", "", children)

    # _ 占位符辅助函数
    def _has_placeholder_node(n):
        if n.type == "ref" and n.value == "_":
            return True
        return any(_has_placeholder_node(c) for c in n.children if isinstance(c, Node))

    def _replace_placeholder_node(n, replacement):
        if n.type == "ref" and n.value == "_":
            return replacement
        new_children = [_replace_placeholder_node(c, replacement) if isinstance(c, Node) else c
                        for c in n.children]
        return Node(n.type, n.value, new_children)

    # 管道: x |> f  或  x |> f(a)  或  x |> f(_, 10)
    pipe_idx = _find_operator(text, "|>")
    if pipe_idx > 0:
        left_text = text[:pipe_idx].strip()
        right_text = text[pipe_idx + 2:].strip()
        if left_text and right_text:
            left = _parse_expr(left_text)
            right = _parse_expr(right_text)
            # _ 占位符：x |> f(_, 10) → f(x, 10)
            if _has_placeholder_node(right):
                return _replace_placeholder_node(right, left)
            # 脱糖：x |> f → f(x)，x |> f(a) → f(x, a)
            if right.type == "call":
                new_children = [right.children[0], left] + right.children[1:]
                return Node("call", "", new_children)
            elif right.type == "ref":
                return Node.call(right, [left])
            else:
                return Node("call", "", [right, left])

    # 二元操作符（从右到左，低优先级的先检查）
    # 必须先于函数调用，以免 n * f(x) 被误认为函数调用
    # 顺序：or → and → 比较 → 算术（低→高优先级）
    for op in ["or", "and", "==", "!=", ">=", "<=", ">", "<", "+", "-", "*", "/", "%"]:
        idx = _find_operator(text, op)
        if idx > 0:
            left = text[:idx].strip()
            right = text[idx + len(op):].strip()
            if left and right:
                return Node.op(op, _parse_expr(left), _parse_expr(right))

    # 分组括号：(expr) — 不是函数调用，只是分组
    if text.startswith("(") and text.endswith(")"):
        # 检查第一个 ( 和最后一个 ) 是否配对
        depth = 0
        match_end = -1
        for ci, ch in enumerate(text):
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    match_end = ci
                    break
        if match_end == len(text) - 1:
            # 第一个 ( 匹配到最后一个 )，是分组括号
            inner = text[1:-1].strip()
            if inner:
                return _parse_expr(inner)

    # 函数调用：name(args)
    if "(" in text and text.endswith(")"):
        name_end = text.index("(")
        name = text[:name_end].strip()
        args_text = text[name_end + 1:-1].strip()
        args = []
        if args_text:
            args = [_parse_expr(a.strip()) for a in _split_comma(args_text)]
        # 前缀运算符：+(a,b) → op(+, a, b)
        _prefix_ops = {"+", "-", "*", "/", "%", "==", "!=", "<", ">", "<=", ">=", "and", "or", "not"}
        if name in _prefix_ops:
            if len(args) == 1:
                return Node.op(name, args[0])
            elif len(args) == 2:
                return Node.op(name, args[0], args[1])
        return Node.call(Node.ref(name), args)

    # 引用（变量名）
    if text.isidentifier() or (text.startswith("_") and text[1:].isidentifier()):
        return Node.ref(text)

    # 索引操作：a[b] (支持链式 a[b][c])
    if "[" in text and text.endswith("]"):
        idx = text.index("[")
        # 找到匹配的 ]
        depth = 0
        match_bracket = -1
        for bi in range(idx, len(text)):
            if text[bi] == "[":
                depth += 1
            elif text[bi] == "]":
                depth -= 1
                if depth == 0:
                    match_bracket = bi
                    break
        if match_bracket == len(text) - 1:
            # a[b] — 单层索引
            base = text[:idx].strip()
            index_expr = text[idx + 1:match_bracket].strip()
            return Node("index", children=[_parse_expr(base), _parse_expr(index_expr)])
        elif match_bracket > 0:
            # a[b][c] — 链式索引
            base_node = _parse_expr(text[:match_bracket + 1])
            inner_text = text[match_bracket + 2:-1].strip()
            return Node("index", children=[base_node, _parse_expr(inner_text)])

    # 点操作：a.b.c → a["b"]["c"]（链式多点+索引，支持 a.b[0].c）
    if "." in text:
        # 仅当点在外层（非括号/字符串内）时才做点表达式解析
        depth = 0
        has_depth0_dot = False
        for ch in text:
            if ch in "[({": depth += 1
            elif ch in "])}": depth -= 1
            elif ch == "." and depth == 0: has_depth0_dot = True; break
        if has_depth0_dot:
            return _parse_dot_expression(text)

    return Node.ref(text)


def _find_operator(text: str, op: str) -> int:
    """从右到左找操作符位置（跳过括号和字符串内的）。"""
    paren_depth = 0
    in_string = False
    string_char = None
    max_start = len(text) - len(op)
    for i in range(len(text) - 1, -1, -1):
        ch = text[i]
        # 字符串追踪（全范围扫描，避免范围截断导致 open/close 误判）
        if ch in ('"', "'"):
            if i > 0 and text[i - 1] == '\\':
                pass  # 转义引号，忽略
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
            # 跳过 == 等双目操作符前后的 =
            if op == "=":
                if i > 0 and text[i - 1] in ("!", "=", ">", "<"):
                    continue
                if i + 1 < len(text) and text[i + 1] == "=":
                    continue
            # 单词操作符（or, and, not）需要边界检查，避免 floor 中的 or
            if op.isalpha():
                if i > 0 and (text[i-1].isalnum() or text[i-1] == '_'):
                    continue
                after = i + len(op)
                if after < len(text) and (text[after].isalnum() or text[after] == '_'):
                    continue
            return i
    return -1


def _split_comma(text: str) -> list[str]:
    """按逗号分割（跳过括号内的）。"""
    parts = []
    depth = 0
    current = ""
    in_str = False
    str_ch = None
    for ci, ch in enumerate(text):
        if ch in ('"', "'") and not in_str:
            in_str = True
            str_ch = ch
            current += ch
        elif in_str and ch == str_ch:
            num_bs = 0
            k = ci - 1
            while k >= 0 and text[k] == '\\':
                num_bs += 1
                k -= 1
            if num_bs % 2 == 0:
                in_str = False
                str_ch = None
            current += ch
        elif not in_str:
            if ch in "([{":
                depth += 1
                current += ch
            elif ch in ")]}":
                depth -= 1
                current += ch
            elif ch == "," and depth == 0:
                parts.append(current.strip())
                current = ""
            else:
                current += ch
        else:
            current += ch
    if current.strip():
        parts.append(current.strip())
    return parts


def _split_body_statements(body_text: str) -> list[str]:
    """按顶层语句分割 body（跟踪括号深度，支持单行多语句）。"""
    lines = body_text.split("\n")
    stmts = []
    current = []
    depth = 0
    for line in lines:
        stripped = line.strip()
        if not stripped or stripped.startswith("//") or stripped.startswith(";;"):
            continue
        in_str = False
        str_ch = None
        seg_start = 0
        for ci, ch in enumerate(line):
            if in_str:
                if ch == "\\" and ci + 1 < len(line):
                    continue
                if ch == str_ch:
                    in_str = False
                continue
            if ch in ('"', "'"):
                in_str = True
                str_ch = ch
                continue
            if ch in "([{":
                depth += 1
            elif ch in ")]}":
                depth -= 1
                if depth == 0 and ch == "}":
                    # 只在 } 处切分（不切 )，保持 if/while/for 的条件+体在一起
                    seg = line[seg_start:ci + 1].strip()
                    if seg:
                        current.append(seg)
                        joined = "\n".join(current)
                        # 隐式 else: } { 保持在一起
                        if ci + 1 < len(line):
                            rest = line[ci + 1:].lstrip()
                            # 隐式 else: } { 或 try/catch: } catch
                            if rest.startswith("{") or rest.startswith("catch"):
                                seg_start = ci + 1
                                continue
                            # } 后面有新语句 → 切分
                            stmts.append(joined)
                            current = []
                        else:
                            # } 后面没东西 → 不切，等下一行
                            # （嵌套 if/else 尾部闭合不应切碎）
                            pass
                    seg_start = ci + 1
        remaining = line[seg_start:].strip()
        if remaining:
            current.append(remaining)
            if depth == 0:
                stmts.append("\n".join(current))
                current = []
    if current:
        stmts.append("\n".join(current))
    return stmts


# ═══════════════════════════════════════════
#  导入系统
# ═══════════════════════════════════════════

# ═══════════════════════════════════════════

# Import 正则（中文关键字：引 "path" 别 alias）
_import_re = re.compile(r'^引\s+"([^"]+)"\s+别\s+(\S+)\s*$')
_import_re_single = re.compile(r"^引\s+'([^']+)'\s+别\s+(\S+)\s*$")


def _parse_imports(source: str) -> list[tuple[str, Optional[str]]]:
    """提取源码中的 import 指令。

    返回 [(path, alias), ...]
    语法：引 "path" 别 alias
    """
    imports = []
    for line in source.split("\n"):
        stripped = line.strip()
        if not stripped or stripped.startswith("//") or stripped.startswith(";;"):
            continue
        m = _import_re.match(stripped) or _import_re_single.match(stripped)
        if m:
            imports.append((m.group(1), m.group(2)))
    return imports


def _strip_imports(source: str) -> str:
    """移除源码中的 import 行（parser 不处理 import）。"""
    lines = []
    for line in source.split("\n"):
        stripped = line.strip()
        if not stripped or stripped.startswith("//") or stripped.startswith(";;"):
            lines.append(line)
            continue
        if _import_re.match(stripped) or _import_re_single.match(stripped):
            continue
        lines.append(line)
    return "\n".join(lines)


def _resolve_import_path(import_path: str, current_dir: Optional[str] = None) -> Optional[Path]:
    """解析导入路径为绝对文件路径。

    解析规则：
    1. 如果以 '.' 开头，相对于 current_dir
    2. 如果是绝对路径，直接使用
    3. 否则在 DAO_HOME 下查找
    4. 自动补充 .ku 扩展名
    """
    p = Path(import_path)

    # 有扩展名直接尝试
    candidates = []
    p_path = Path(import_path)

    if import_path.startswith("."):
        # 相对路径
        if current_dir:
            base = Path(current_dir)
        else:
            base = Path.cwd()
        candidates.append(base / p_path)
        candidates.append(base / p_path.with_suffix(".ku"))
    elif p_path.is_absolute():
        candidates.append(p_path)
        candidates.append(p_path.with_suffix(".ku"))
    else:
        # 在 DAO_HOME / std 下查找
        candidates.append(DAO_HOME / p_path)
        candidates.append(DAO_HOME / p_path.with_suffix(".ku"))
        # 也查找原始路径（如果是完整路径）
        candidates.append(Path.cwd() / p_path)
        candidates.append(Path.cwd() / p_path.with_suffix(".ku"))

    # 去重并返回第一个存在的
    seen = set()
    for c in candidates:
        resolved = c.resolve()
        if str(resolved) not in seen:
            seen.add(str(resolved))
            if resolved.exists():
                return resolved
    return None


# ═══════════════════════════════════════════
#  运行时环境
# ═══════════════════════════════════════════

class DaoEnv:
    """
    ku 运行时环境。

    管理：
    - thought 注册表（所有已加载的 thought）
    - 持久化（保存/加载到磁盘）
    - Python 互操作（导出为 Python 可调用）
    """

    def __init__(self):
        self.loaded_from = None
        self.load_conflicts = []
        _inject_builtins(self)

    def set(self, name: str, value):
        """在运行时环境中设置一个值（非 Thought），对所有 thought 可见。"""
        Thought.registry[name] = value

    @property
    def registry(self):
        return Thought.registry

    def _register_loaded_thought(self, thought: Thought, source: Optional[str], name: Optional[str] = None) -> bool:
        reg_name = name or thought.name
        existing = Thought.registry.get(reg_name)
        new_source = str(source or "")
        if new_source:
            thought.meta["source"] = new_source

        if existing is not None and existing.meta.get("builtin"):
            return False

        if existing is not None and existing is not thought:
            old_source = existing.meta.get("source", "")
            if old_source != new_source:
                conflict = {
                    "name": reg_name,
                    "previous_source": old_source,
                    "new_source": new_source,
                    "overwritten": True,
                }
                self.load_conflicts.append(conflict)
                Thought.load_conflicts.append(conflict)

        Thought.registry[reg_name] = thought
        return True

    def _import_module(self, import_path: str, prefix: Optional[str] = None,
                        current_dir: Optional[str] = None,
                        visited: Optional[set] = None) -> int:
        """递归导入一个模块。

        返回导入的 thought 数量。
        """
        if visited is None:
            visited = set()

        resolved = _resolve_import_path(import_path, current_dir)
        if resolved is None:
            raise FileNotFoundError(f"ku: 找不到模块 '{import_path}' (搜索路径: {current_dir or 'DAO_HOME'})")

        resolved_str = str(resolved)
        if resolved_str in visited:
            return 0  # 已导入，跳过
        visited.add(resolved_str)

        source = resolved.read_text(encoding="utf-8")
        current_dir = str(resolved.parent)

        # 先处理子模块的 import
        sub_imports = _parse_imports(source)
        for sub_path, sub_prefix in sub_imports:
            self._import_module(sub_path, sub_prefix, current_dir, visited)

        # 解析 thought
        clean_source = _strip_imports(source)
        thoughts = parse_道(clean_source)

        count = 0
        for t in thoughts:
            if prefix:
                # 带命名空间的导入：thought 名称加前缀
                namespaced_name = f"{prefix}_{t.name}"
                t.name = namespaced_name
                # 如果注册表已有同名 thought，覆盖
                Thought.registry[namespaced_name] = t
            else:
                # 直接导入：可能覆盖已有的 thought
                Thought.registry[t.name] = t
            count += 1

        return count

    # ── Import 机制 ──

    def _resolve_import_path(self, path: str, current_dir: Optional[str] = None) -> str:
        """解析 import 路径为绝对路径。

        支持：
        - std/xxx → DAO_DIR/std/xxx.ku
        - 绝对路径 → 直接用
        - 相对路径 → 相对于 current_dir 或 loaded_from
        """
        if path.startswith("std/") or path.startswith("std\\"):
            dao_dir = getattr(self, '_dao_dir', os.path.dirname(os.path.abspath(__file__)))
            resolved = os.path.join(dao_dir, path)
        elif os.path.isabs(path):
            resolved = path
        else:
            base = current_dir or getattr(self, '_current_file_dir', None) or \
                   os.path.dirname(getattr(self, 'loaded_from', '')) or os.getcwd()
            resolved = os.path.normpath(os.path.join(base, path))
        if not resolved.endswith(".ku"):
            resolved += ".ku"
        return resolved

    def _import_module(self, path: str, prefix: str, current_dir: Optional[str] = None):
        """加载一个 .ku 文件作为模块，用 prefix 作为命名空间前缀注册到 registry。"""
        resolved = self._resolve_import_path(path, current_dir)
        if not os.path.exists(resolved):
            print(f"[IMPORT] warning: file not found: {resolved}")
            return

        # 模块缓存
        if not hasattr(self, '_module_cache'):
            self._module_cache = {}
        if resolved in self._module_cache:
            return  # 已加载过，跳过

        self._module_cache[resolved] = True  # 标记加载中，防止循环引用

        saved = getattr(self, '_current_file_dir', None)
        self._current_file_dir = os.path.dirname(resolved)
        try:
            sub_env = DaoEnv()
            sub_env._dao_dir = getattr(self, '_dao_dir', os.path.dirname(os.path.abspath(__file__)))
            sub_env._module_cache = self._module_cache  # 共享缓存
            thoughts = sub_env.load(resolved, process_imports=True)
            self.load_conflicts.extend(sub_env.load_conflicts)
            for t in thoughts:
                if prefix:
                    namespaced = f"{prefix}_{t.name}"
                    self._register_loaded_thought(t, resolved, namespaced)
                else:
                    self._register_loaded_thought(t, resolved)
            print(f"[IMPORT] loaded {len(thoughts)} thoughts from {resolved}")
        except Exception as e:
            print(f"[IMPORT] error loading {resolved}: {e}")
        finally:
            self._current_file_dir = saved

    def load(self, source_or_path: str, process_imports: bool = True) -> list[Thought]:
        """加载 ku 源码（文件路径或源码字符串）。

        如果 process_imports 为 True，自动处理源码中的 import 指令。
        返回解析出的 thought 列表。
        """
        path = Path(source_or_path)
        if path.exists():
            source = path.read_text(encoding="utf-8")
            self.loaded_from = str(path)
        else:
            source = source_or_path
            path = None

        # 处理 import 指令
        if process_imports:
            imports = _parse_imports(source)
            current_dir = str(path.parent) if path and path.parent else None
            for imp_path, prefix in imports:
                self._import_module(imp_path, prefix, current_dir)

        # 移除 import 行后解析
        clean_source = _strip_imports(source) if process_imports else source
        thoughts = parse_道(clean_source)
        # 注册到 registry（不覆盖 builtin）
        for t in thoughts:
            self._register_loaded_thought(t, str(path) if path else "<inline>")
        return thoughts

    def load_dir(self, dir_path: str, process_imports: bool = True):
        """加载目录下所有 .ku 文件。"""
        p = Path(dir_path)
        if not p.exists():
            return []
        all_thoughts = []
        for f in sorted(p.glob("*.ku")):
            all_thoughts.extend(self.load(str(f), process_imports=process_imports))
        return all_thoughts

    def save_state(self, path: Optional[str] = None):
        """将所有 thought 持久化到磁盘。"""
        save_path = Path(path or str(DAO_DATA_DIR / ".ku_state.json"))
        save_path.parent.mkdir(parents=True, exist_ok=True)

        data = {}
        for name, thought in Thought.registry.items():
            if not isinstance(thought.body, Node):
                continue  # 跳过 Python 内置 thought
            data[name] = {
                "name": thought.name,
                "params": thought.params,
                "body": thought.body.to_dict() if thought.body else None,
                "doc": thought.doc,
                "meta": thought.meta,
            }

        save_path.write_text(
            json.dumps(data, ensure_ascii=True, indent=2),
            encoding="utf-8"
        )
        return str(save_path)

    def load_state(self, path: str) -> int:
        """从磁盘恢复 thought 状态。"""
        p = Path(path)
        if not p.exists():
            return 0

        data = json.loads(p.read_text(encoding="utf-8"))
        count = 0
        for name, d in data.items():
            body = Node.from_dict(d["body"]) if d.get("body") else None
            Thought(
                name=d["name"],
                params=d.get("params", []),
                body=body,
                doc=d.get("doc", ""),
                meta=d.get("meta", {}),
            )
            count += 1
        return count

    def export_python(self, thought_names: Optional[list[str]] = None) -> str:
        """将指定 thought 导出为 Python 代码。"""
        names = thought_names or list(Thought.registry.keys())
        parts = []
        for name in names:
            if name in Thought.registry:
                parts.append(Thought.registry[name].to_python())
        return "\n\n".join(parts)

    def get(self, name: str) -> Optional[Thought]:
        """获取已注册的 thought。"""
        return Thought.registry.get(name)

    def run(self, name: str, args: Optional[list] = None) -> Any:
        """运行指定的 thought。"""
        thought = self.get(name)
        if not thought:
            raise KeyError(f"thought '{name}' 未找到")
        return thought.call(args)

    def stats(self) -> dict:
        """运行时统计。"""
        return {
            "thoughts": sum(1 for t in Thought.registry.values() if isinstance(t, Thought)),
            "executions": sum(t.meta.get("executions", 0) for t in Thought.registry.values() if isinstance(t, Thought)),
            "log_entries": len(Thought.execution_log),
        }

    def reset(self):
        """重置运行时（清空所有 thought）。"""
        Thought.registry.clear()
        Thought.execution_log.clear()


# ═══════════════════════════════════════════
#  工具函数
# ═══════════════════════════════════════════

def _clone_node(node: Optional[Node]) -> Optional[Node]:
    """深克隆 AST 节点。"""
    if node is None:
        return None
    return Node(
        type=node.type,
        value=node.value,
        children=[_clone_node(c) for c in node.children],
        meta=dict(node.meta),
    )


def _simplify(val: Any) -> Any:
    """将值简化为 JSON 友好格式。"""
    if isinstance(val, (int, float, str, bool, type(None))):
        return val
    if isinstance(val, (list, tuple)):
        return [_simplify(v) for v in val[:10]]  # 限制长度
    if isinstance(val, dict):
        return {str(k): _simplify(v) for k, v in list(val.items())[:10]}
    if isinstance(val, Thought):
        return f"Thought({val.name})"
    return repr(val)[:200]


# ═══════════════════════════════════════════
#  内置 thought（用 Python 注入，是引导阶段的桥接）
# ═══════════════════════════════════════════

def _inject_builtins(env: DaoEnv):
    """注入内置 thought（用 Python 实现，ku 自举后可被替代）。"""
    builtins = {
        "print": Thought("print", ["x"], Node.lit(None), doc="输出到控制台"),
        "len": Thought("len", ["x"], Node.lit(None), doc="获取长度"),
        "type": Thought("type", ["x"], Node.lit(None), doc="获取类型"),
        "now": Thought("now", [], Node.lit(None), doc="当前时间戳"),
        "now_fmt": Thought("now_fmt", [], Node.lit(None), doc="当前本地时间字符串 YYYY-MM-DD HH:MM:SS"),
        "range": Thought("range", ["n"], Node.lit(None), doc="生成范围"),
        "save": Thought("save", [], Node.lit(None), doc="保存运行时状态"),
        "load": Thought("load", ["path"], Node.lit(None), doc="加载运行时状态"),
        "read_file": Thought("read_file", ["path"], Node.lit(None), doc="读取文件"),
        "write_file": Thought("write_file", ["path", "content"], Node.lit(None), doc="写入文件"),
        "json_parse": Thought("json_parse", ["text"], Node.lit(None), doc="解析 JSON"),
        "json_stringify": Thought("json_stringify", ["obj"], Node.lit(None), doc="序列化 JSON"),
        "run_bytecode": Thought("run_bytecode", ["bytecode"], Node.lit(None), doc="执行字节码"),
        # HTTP & 网络
        "http_get": Thought("http_get", ["url", "headers"], Node.lit(None), doc="HTTP GET 请求"),
        "http_post": Thought("http_post", ["url", "body", "headers"], Node.lit(None), doc="HTTP POST 请求"),
        # 系统
        "system": Thought("system", ["cmd"], Node.lit(None), doc="执行 shell 命令"),
        # 文件系统
        "list_dir": Thought("list_dir", ["path"], Node.lit(None), doc="列出目录文件"),
        "path_exists": Thought("path_exists", ["path"], Node.lit(None), doc="检查路径是否存在"),
        "mkdir": Thought("mkdir", ["path"], Node.lit(None), doc="创建目录"),
        "delete_file": Thought("delete_file", ["path"], Node.lit(None), doc="删除文件"),
        # 字符串操作
        "str_contains": Thought("str_contains", ["s", "substr"], Node.lit(None), doc="字符串包含"),
        "str_replace": Thought("str_replace", ["s", "old", "new"], Node.lit(None), doc="字符串替换"),
        "str_split": Thought("str_split", ["s", "delimiter"], Node.lit(None), doc="字符串分割"),
        "str_trim": Thought("str_trim", ["s"], Node.lit(None), doc="去除首尾空白"),
        "str_lower": Thought("str_lower", ["s"], Node.lit(None), doc="转小写"),
        "str_upper": Thought("str_upper", ["s"], Node.lit(None), doc="转大写"),
        "str_starts_with": Thought("str_starts_with", ["s", "prefix"], Node.lit(None), doc="检查前缀"),
        "str_ends_with": Thought("str_ends_with", ["s", "suffix"], Node.lit(None), doc="检查后缀"),
        "str_format": Thought("str_format", ["template", "values"], Node.lit(None), doc="字符串格式化"),
        "str_is_empty": Thought("str_is_empty", ["s"], Node.lit(None), doc="检查字符串是否为空或空白"),
        # 数学
        "floor": Thought("floor", ["n"], Node.lit(None), doc="向下取整"),
        "ceil": Thought("ceil", ["n"], Node.lit(None), doc="向上取整"),
        "abs": Thought("abs", ["n"], Node.lit(None), doc="绝对值"),
        "max": Thought("max", ["a", "b"], Node.lit(None), doc="取较大值"),
        "min": Thought("min", ["a", "b"], Node.lit(None), doc="取较小值"),
        "round": Thought("round", ["n"], Node.lit(None), doc="四舍五入"),
        # 反射（自修改核心）
        "parse": Thought("parse", ["code"], Node.lit(None), doc="解析 ku 代码为 AST 字典"),
        "eval_ku": Thought("eval_ku", ["code"], Node.lit(None), doc="运行时执行 ku 代码"),
        "create_thought": Thought("create_thought", ["name", "params_json", "code"], Node.lit(None), doc="动态创建 thought"),
        "clone_thought": Thought("clone_thought", ["name", "new_name"], Node.lit(None), doc="克隆 thought"),
        "delete_thought": Thought("delete_thought", ["name"], Node.lit(None), doc="删除 thought"),
        "list_thoughts": Thought("list_thoughts", [], Node.lit(None), doc="列出所有 thought"),
        "get_thought": Thought("get_thought", ["name"], Node.lit(None), doc="获取 thought 对象"),
        "thought_registry": Thought("thought_registry", [], Node.lit(None), doc="获取注册表快照"),
        "get_log": Thought("get_log", ["n"], Node.lit(None), doc="获取执行日志"),
        "clear_log": Thought("clear_log", [], Node.lit(None), doc="清空执行日志"),
        # 杂项
        "exit": Thought("exit", ["code"], Node.lit(None), doc="退出进程"),
        "sleep": Thought("sleep", ["seconds"], Node.lit(None), doc="等待指定秒数"),
        # 基础操作
        "ord": Thought("ord", ["ch"], Node.lit(None), doc="字符转 ASCII"),
        "chr": Thought("chr", ["n"], Node.lit(None), doc="ASCII 转字符"),
        "push": Thought("push", ["lst", "val"], Node.lit(None), doc="列表追加"),
        "has": Thought("has", ["obj", "key"], Node.lit(None), doc="检查键/索引是否存在"),
        "slice": Thought("slice", ["obj", "start", "end"], Node.lit(None), doc="切片"),
        "int": Thought("int", ["x"], Node.lit(None), doc="转整数"),
        "str": Thought("str", ["x"], Node.lit(None), doc="转字符串"),
        "float": Thought("float", ["x"], Node.lit(None), doc="转浮点数"),
        "bool": Thought("bool", ["x"], Node.lit(None), doc="转布尔"),
        # SQLite
        "sqlite_open": Thought("sqlite_open", ["path"], Node.lit(None), doc="打开 SQLite 数据库"),
        "sqlite_exec": Thought("sqlite_exec", ["conn", "sql", "params"], Node.lit(None), doc="执行 SQL（INSERT/UPDATE/DELETE/CREATE）"),
        "sqlite_query": Thought("sqlite_query", ["conn", "sql", "params"], Node.lit(None), doc="查询 SQL，返回字典列表"),
        "sqlite_close": Thought("sqlite_close", ["conn"], Node.lit(None), doc="关闭数据库连接"),
        # 运行时数据目录（经验/记忆/数据集持久化的统一根）
        "dao_data_dir": Thought("dao_data_dir", [], Node.lit(None), doc="返回 Dao 运行时数据目录（自动创建）"),
        "dao_data_path": Thought("dao_data_path", ["name"], Node.lit(None), doc="返回 Dao 数据目录下的子路径（父目录自动创建）"),
        # 类型检查
        "is_list": Thought("is_list", ["x"], Node.lit(None), doc="检查是否为列表"),
        "is_dict": Thought("is_dict", ["x"], Node.lit(None), doc="检查是否为字典"),
        "is_str": Thought("is_str", ["x"], Node.lit(None), doc="检查是否为字符串"),
        "is_int": Thought("is_int", ["x"], Node.lit(None), doc="检查是否为整数"),
        "is_none": Thought("is_none", ["x"], Node.lit(None), doc="检查是否为 None"),
        "keys": Thought("keys", ["d"], Node.lit(None), doc="获取字典所有键"),
        "items": Thought("items", ["d"], Node.lit(None), doc="获取字典所有键值对"),
        "values": Thought("values", ["d"], Node.lit(None), doc="获取字典所有值"),
        "merge": Thought("merge", ["a", "b"], Node.lit(None), doc="合并两个字典"),
        "to_float": Thought("to_float", ["x"], Node.lit(None), doc="转浮点数（别名）"),
    }

    # ── 中文别名 ──
    _cn_aliases = {
        "示": "print",
        "写文件": "write_file",
        "读文件": "read_file",
        "文_分割": "str_split",
        "文_替换": "str_replace",
        "文_包含": "str_contains",
        "文_去空白": "str_trim",
        "文_是否为空": "str_is_empty",
        "文_小写": "str_lower",
        "文_大写": "str_upper",
        "文_起头": "str_starts_with",
        "文_结尾": "str_ends_with",
        "文_格式化": "str_format",
        "列表长度": "len",
        "类型": "type",
        "范围": "range",
        "整数": "int",
        "浮点数": "float",
        "布尔": "bool",
        "是字符串": "is_str",
        "是列表": "is_list",
        "是字典": "is_dict",
        "是整数": "is_int",
        "是空": "is_none",
        "字典键": "keys",
        "字典值": "values",
        "合并": "merge",
        "有序": "ord",
        "字符": "chr",
        "追加": "push",
        "含有": "has",
        "切片": "slice",
    }
    for cn, en in _cn_aliases.items():
        if en in builtins:
            builtins[cn] = builtins[en]

    # ── 文_转字符串 独立函数（不共用 str 对象，避免 std/string.ku 覆盖）──
    def _文_转字符串(x):
        return str(x)
    builtins["文_转字符串"] = Thought("文_转字符串", ["x"], Node.lit(None), doc="转字符串")
    builtins["文_转字符串"].body = _文_转字符串

    # ── 用 Python 函数覆盖 body ──

    def _print(x):
        try:
            print(x)
        except UnicodeEncodeError:
            sys.stdout.buffer.write((str(x) + "\n").encode("utf-8", errors="replace"))
        return x
    def _len(x):
        return len(x) if hasattr(x, "__len__") else 0
    def _type(x):
        return type(x).__name__
    def _now():
        return time.time()
    def _now_fmt():
        return time.strftime("%Y-%m-%d %H:%M:%S", time.localtime())
    def _range(n):
        return list(range(n))
    builtins["print"].body = _print
    builtins["len"].body = _len
    builtins["type"].body = _type
    builtins["now"].body = _now
    builtins["now_fmt"].body = _now_fmt
    builtins["range"].body = _range

    def _save():
        return env.save_state()
    builtins["save"].body = _save
    def _load(path):
        return env.load_state(path)
    builtins["load"].body = _load
    def _read_file(path):
        with open(path, "r", encoding="utf-8") as f:
            return f.read()
    builtins["read_file"].body = _read_file
    def _write_file(path, content):
        with open(path, "w", encoding="utf-8") as f:
            f.write(str(content))
        return True
    builtins["write_file"].body = _write_file
    def _json_parse(text):
        return json.loads(text)
    builtins["json_parse"].body = _json_parse
    def _json_stringify(obj):
        return json.dumps(obj, ensure_ascii=False, indent=2)
    builtins["json_stringify"].body = _json_stringify
    def _run_bytecode(bytecode):
        try:
            from .compiler import DaoVM
        except (ImportError, SystemError):
            from compiler import DaoVM
        return DaoVM().execute(bytecode)
    builtins["run_bytecode"].body = _run_bytecode

    # ── 基础操作 ──
    def _ord(ch):
        return ord(ch) if isinstance(ch, str) and len(ch) > 0 else 0
    def _chr(n):
        return chr(int(n))
    def _push(lst, val):
        if isinstance(lst, list):
            lst.append(val)
            return lst
        return [val]
    def _has(obj, key):
        if isinstance(obj, dict):
            return key in obj
        if isinstance(obj, (list, str)):
            return 0 <= key < len(obj)
        return False
    def _slice(obj, start, end=None):
        if end is None:
            end = len(obj)
        return obj[int(start):int(end)]
    def _int_val(x):
        return int(x)
    def _str_val(x):
        return str(x)
    def _float_val(x):
        return float(x)
    def _bool_val(x):
        return bool(x)
    builtins["ord"].body = _ord
    builtins["chr"].body = _chr
    builtins["push"].body = _push
    builtins["has"].body = _has
    builtins["slice"].body = _slice
    builtins["int"].body = _int_val
    builtins["str"].body = _str_val
    builtins["float"].body = _float_val
    builtins["bool"].body = _bool_val

    # ── SQLite ──
    import sqlite3 as _sqlite3
    _db_connections = {}  # path → connection cache

    def _sqlite_open(path):
        p = str(path)
        if p not in _db_connections or _db_connections[p] is None:
            conn = _sqlite3.connect(p)
            conn.row_factory = _sqlite3.Row
            _db_connections[p] = conn
        return _db_connections[p]

    def _sqlite_exec(conn, sql, params=None):
        cursor = conn.execute(sql, params or [])
        conn.commit()
        return cursor.rowcount

    def _sqlite_query(conn, sql, params=None):
        rows = conn.execute(sql, params or []).fetchall()
        return [dict(r) for r in rows]

    def _sqlite_close(conn):
        try:
            conn.close()
            # 从缓存中移除
            for k, v in list(_db_connections.items()):
                if v is conn:
                    _db_connections[k] = None
                    break
        except:
            pass

    builtins["sqlite_open"].body = _sqlite_open
    builtins["sqlite_exec"].body = _sqlite_exec
    builtins["sqlite_query"].body = _sqlite_query
    builtins["sqlite_close"].body = _sqlite_close

    # ── 运行时数据目录 ──
    def _dao_data_dir():
        DAO_DATA_DIR.mkdir(parents=True, exist_ok=True)
        return str(DAO_DATA_DIR)

    def _dao_data_path(name):
        p = DAO_DATA_DIR / str(name)
        p.parent.mkdir(parents=True, exist_ok=True)
        return str(p)

    builtins["dao_data_dir"].body = _dao_data_dir
    builtins["dao_data_path"].body = _dao_data_path

    # ── 类型检查 & 字典操作 ──
    builtins["is_list"].body = lambda x: isinstance(x, list)
    builtins["is_dict"].body = lambda x: isinstance(x, dict)
    builtins["is_str"].body = lambda x: isinstance(x, str)
    builtins["is_int"].body = lambda x: isinstance(x, int)
    builtins["is_none"].body = lambda x: x is None
    builtins["keys"].body = lambda d: list(d.keys()) if isinstance(d, dict) else []
    builtins["items"].body = lambda d: [[k, v] for k, v in d.items()] if isinstance(d, dict) else []
    builtins["values"].body = lambda d: list(d.values()) if isinstance(d, dict) else []
    builtins["merge"].body = lambda a, b: {**a, **b} if isinstance(a, dict) and isinstance(b, dict) else a
    builtins["to_float"].body = lambda x: float(x)

    # ── 运算符 builtins（Ku parser 用 call 形式调用） ──
    for op_name in ["+", "-", "*", "/", "%", "==", "!=", "<", ">", "<=", ">="]:
        builtins[op_name] = Thought(op_name, ["a", "b"], Node.lit(None))
    builtins["+"].body = lambda a, b: a + b
    builtins["-"].body = lambda a, b: a - b
    builtins["*"].body = lambda a, b: a * b
    builtins["/"].body = lambda a, b: a / b
    builtins["%"].body = lambda a, b: a % b
    builtins["=="].body = lambda a, b: a == b
    builtins["!="].body = lambda a, b: a != b
    builtins["<"].body = lambda a, b: a < b
    builtins[">"].body = lambda a, b: a > b
    builtins["<="].body = lambda a, b: a <= b
    builtins[">="].body = lambda a, b: a >= b
    # 逻辑运算符（支持多参数：and(a,b,c) 短路求值）
    def _and(*args):
        result = True
        for a in args:
            result = a
            if not a:
                return a
        return result
    def _or(*args):
        for a in args:
            if a:
                return a
        return args[-1] if args else False
    builtins["and"] = Thought("and", ["a", "b"], Node.lit(None))
    builtins["and"].body = _and
    builtins["or"] = Thought("or", ["a", "b"], Node.lit(None))
    builtins["or"].body = _or
    builtins["not"] = Thought("not", ["a"], Node.lit(None))
    builtins["not"].body = lambda a: not a

    # ── HTTP & 网络 ──
    import urllib.parse as _urllib_parse

    def _encode_url(url):
        """编码 URL 中的中文字符，保留 ASCII 部分不变"""
        if not isinstance(url, str):
            url = str(url)
        # 如果已编码则直接返回
        if "%" in url:
            return url
        # 分离 scheme://host/path?query
        parsed = _urllib_parse.urlparse(url)
        # 对 path 和 query 做 quote
        safe_path = _urllib_parse.quote(parsed.path, safe="/:@!$&'()*+,;=-._~")
        safe_query = _urllib_parse.quote(parsed.query, safe="/:@!$&'()*+,;=-._~?")
        encoded = _urllib_parse.urlunparse((
            parsed.scheme, parsed.netloc, safe_path,
            parsed.params, safe_query, parsed.fragment
        ))
        return encoded

    def _http_request(url, method="GET", body=None, headers=None):
        """通用 HTTP 请求核心"""
        import urllib.request, urllib.error
        encoded_url = _encode_url(url)
        hdrs = dict(headers or {})
        data = None
        if body is not None:
            if isinstance(body, str):
                data = body.encode("utf-8")
            elif isinstance(body, (dict, list)):
                data = json.dumps(body, ensure_ascii=False).encode("utf-8")
                if "Content-Type" not in hdrs:
                    hdrs["Content-Type"] = "application/json"
            elif isinstance(body, bytes):
                data = body
        req = urllib.request.Request(encoded_url, data=data, headers=hdrs, method=method)
        try:
            with urllib.request.urlopen(req, timeout=60) as resp:
                return {"ok": True, "body": resp.read().decode("utf-8"), "status": resp.status}
        except urllib.error.HTTPError as e:
            err_body = e.read().decode("utf-8", errors="replace") if hasattr(e, 'read') else ""
            return {"ok": False, "error": str(e), "status": e.code, "body": err_body}
        except Exception as e:
            return {"ok": False, "error": str(e), "status": 0}

    def _http_get(url, headers=None):
        return _http_request(url, "GET", headers=headers)

    def _http_post(url, body="", headers=None):
        return _http_request(url, "POST", body=body, headers=headers)

    def _http_put(url, body="", headers=None):
        return _http_request(url, "PUT", body=body, headers=headers)

    def _http_delete(url, headers=None):
        return _http_request(url, "DELETE", headers=headers)

    builtins["http_get"].body = _http_get
    builtins["http_post"].body = _http_post
    builtins["http_put"] = Thought("http_put", ["url", "body", "headers"], Node.lit(None), doc="HTTP PUT 请求")
    builtins["http_put"].body = _http_put
    builtins["http_delete"] = Thought("http_delete", ["url", "headers"], Node.lit(None), doc="HTTP DELETE 请求")
    builtins["http_delete"].body = _http_delete

    # 网络工具
    def _url_encode(text):
        return _urllib_parse.quote(str(text))

    def _url_decode(text):
        return _urllib_parse.unquote(str(text))

    builtins["url_encode"] = Thought("url_encode", ["text"], Node.lit(None), doc="URL 编码")
    builtins["url_encode"].body = _url_encode
    builtins["url_decode"] = Thought("url_decode", ["text"], Node.lit(None), doc="URL 解码")
    builtins["url_decode"].body = _url_decode

    # ── 系统 ──
    def _system(cmd):
        import subprocess
        try:
            result = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=60)
            return {"code": result.returncode, "stdout": result.stdout, "stderr": result.stderr}
        except subprocess.TimeoutExpired:
            return {"code": -1, "stdout": "", "stderr": "timeout"}
        except Exception as e:
            return {"code": -1, "stdout": "", "stderr": str(e)}
    builtins["system"].body = _system

    # ── 文件系统 ──
    def _list_dir(path):
        import os
        try:
            return os.listdir(path)
        except Exception as e:
            return []
    builtins["list_dir"].body = _list_dir

    def _path_exists(path):
        import os
        return os.path.exists(path)
    builtins["path_exists"].body = _path_exists

    def _mkdir(path):
        import os
        os.makedirs(path, exist_ok=True)
        return True
    builtins["mkdir"].body = _mkdir

    def _delete_file(path):
        import os
        try:
            os.remove(path)
            return True
        except:
            return False
    builtins["delete_file"].body = _delete_file

    # ── 字符串 ──
    def _str_contains(s, substr):
        return substr in s
    builtins["str_contains"].body = _str_contains

    def _str_replace(s, old, new):
        return s.replace(old, new)
    builtins["str_replace"].body = _str_replace

    def _str_split(s, delimiter):
        return s.split(delimiter)
    builtins["str_split"].body = _str_split

    def _str_trim(s):
        return s.strip()
    builtins["str_trim"].body = _str_trim

    def _str_lower(s):
        return s.lower()
    builtins["str_lower"].body = _str_lower

    def _str_upper(s):
        return s.upper()
    builtins["str_upper"].body = _str_upper

    def _str_starts_with(s, prefix):
        return s.startswith(prefix)
    builtins["str_starts_with"].body = _str_starts_with

    def _str_ends_with(s, suffix):
        return s.endswith(suffix)
    builtins["str_ends_with"].body = _str_ends_with

    def _str_format(template, values):
        if isinstance(values, list):
            parts = str(template).split("{}")
            result = parts[0] if parts else ""
            for i, val in enumerate(values):
                if i + 1 < len(parts):
                    result += str(val) + parts[i + 1]
                else:
                    result += str(val)
            return result
        return str(template)
    builtins["str_format"].body = _str_format

    def _str_is_empty(s):
        return not s or (isinstance(s, str) and s.strip() == "")
    builtins["str_is_empty"].body = _str_is_empty

    # ── 数学 ──
    def _floor(n):
        import math
        return math.floor(n)
    builtins["floor"].body = _floor

    def _ceil(n):
        import math
        return math.ceil(n)
    builtins["ceil"].body = _ceil

    def _abs(n):
        return abs(n)
    builtins["abs"].body = _abs

    def _max(a, b):
        return a if a > b else b
    builtins["max"].body = _max

    def _min(a, b):
        return a if a < b else b
    builtins["min"].body = _min

    def _round(n):
        return round(n)
    builtins["round"].body = _round

    # ── 反射（核心：自修改、自进化）──
    def _eval_ku(code):
        """在运行时执行 ku 代码字符串。"""
        code = code.strip()
        is_multi = "\n" in code or "{" in code
        ast = _parse_body(code) if is_multi else _parse_expr(code)
        t = env.get("__eval__")
        if not t:
            t = Thought("__eval__", [], Node.lit(None))
        t.body = ast
        return t.call()
    builtins["eval_ku"].body = _eval_ku

    def _parse(code):
        """解析 ku 代码为 AST 字典（数据，不执行）。优先使用 ku_parser（支持 . 属性访问）。"""
        code = code.strip()
        try:
            from .dao_lexer import lex as ku_lex
            from .dao_parser import parse_tokens_as_nodes
        except ImportError:
            from dao_lexer import lex as ku_lex
            from dao_parser import parse_tokens_as_nodes
        tokens = ku_lex(code)
        ast = parse_tokens_as_nodes(tokens)
        # 如果只有一个子节点且不是 block，直接返回那个节点
        if ast.type == "block" and len(ast.children) == 1:
            return ast.children[0].to_dict()
        return ast.to_dict()
    builtins["parse"].body = _parse

    def _create_thought(name, params_json, code):
        """动态创建一个新 thought。"""
        params = json.loads(params_json) if isinstance(params_json, str) else params_json
        is_multi = "\n" in code or "{" in code
        ast = _parse_body(code) if is_multi else _parse_expr(code)
        t = Thought(name, params, ast)
        t.meta["runtime_created"] = True
        t.meta["created_at"] = time.time()
        return f"Thought({name})"
    builtins["create_thought"].body = _create_thought

    def _clone_thought(name, new_name):
        if name not in Thought.registry:
            return f"错误: '{name}' 不存在"
        t = Thought.registry[name]
        if isinstance(t.body, Node):
            clone = t.clone(new_name)
            clone.meta["cloned_at"] = time.time()
            return f"Thought({new_name}) cloned from {name}"
        return f"错误: 无法克隆内置 thought '{name}'"
    builtins["clone_thought"].body = _clone_thought

    def _delete_thought(name):
        if name in Thought.registry:
            del Thought.registry[name]
            return True
        return False
    builtins["delete_thought"].body = _delete_thought

    def _list_thoughts():
        return sorted(Thought.registry.keys())
    builtins["list_thoughts"].body = _list_thoughts

    def _get_thought(name):
        t = Thought.registry.get(name)
        if t:
            return {
                "name": t.name,
                "params": t.params,
                "doc": t.doc,
                "meta": dict(t.meta),
                "body_type": type(t.body).__name__,
            }
        return None
    builtins["get_thought"].body = _get_thought

    def _thought_registry():
        result = {}
        for name, t in Thought.registry.items():
            result[name] = {
                "name": t.name,
                "params": t.params,
                "doc": t.doc,
                "meta": dict(t.meta),
                "body_type": type(t.body).__name__,
            }
        return result
    builtins["thought_registry"].body = _thought_registry

    def _get_log(n=50):
        """返回最近 N 条执行日志。"""
        log = Thought.execution_log
        return log[-n:] if n < len(log) else log
    builtins["get_log"].body = _get_log

    def _clear_log():
        Thought.execution_log.clear()
        return True
    builtins["clear_log"].body = _clear_log

    # ── 杂项 ──
    def _exit(code=0):
        sys.exit(code)
    builtins["exit"].body = _exit

    def _sleep(seconds):
        time.sleep(seconds)
        return True
    builtins["sleep"].body = _sleep

    for t in builtins.values():
        t.meta["builtin"] = True
        t.meta["source"] = "<builtin>"

    # ── 中文别名注册到 registry ──
    for cn in _cn_aliases:
        if cn in builtins:
            Thought.registry[cn] = builtins[cn]
    if "文_转字符串" in builtins:
        Thought.registry["文_转字符串"] = builtins["文_转字符串"]


# ═══════════════════════════════════════════
#  CLI 入口
# ═══════════════════════════════════════════

def main():
    """ku 命令行入口。"""
    import argparse

    parser = argparse.ArgumentParser(description="ku — 天书母语运行时")
    parser.add_argument("-f", "--file", metavar="PATH",
                        help=".ku 源码文件")
    parser.add_argument("-e", "--exec", metavar="EXPR",
                        help="直接执行表达式")
    parser.add_argument("-r", "--run", metavar="THOUGHT",
                        help="运行指定 thought")
    parser.add_argument("args", nargs="*",
                        help="传递给 thought 的参数")
    parser.add_argument("--save", action="store_true",
                        help="执行后保存状态")
    parser.add_argument("--load", metavar="PATH",
                        help="从状态文件恢复")
    parser.add_argument("--py", metavar="THOUGHT",
                        help="将 thought 转译为 Python")
    parser.add_argument("--stats", action="store_true",
                        help="显示运行时统计")
    parser.add_argument("--reset", action="store_true",
                        help="重置运行时")
    parser.add_argument("-d", "--daemon", action="store_true",
                        help="启动持久化 REPL")
    parser.add_argument("--compile", action="store_true",
                        help="编译 .ku 文件为字节码 (.kub)")
    parser.add_argument("-o", "--output", metavar="PATH",
                        help="字节码输出路径（与 --compile 配合）")
    parser.add_argument("--dis", metavar="PATH",
                        help="反汇编 .kub 字节码文件")

    args = parser.parse_args()

    env = DaoEnv()
    _inject_builtins(env)
    _inject_parser_to_env(env)
    _inject_compiler_to_env(env)

    # 自动加载 base.ku
    base_path = DAO_HOME / "base.ku"
    if base_path.exists():
        env.load(str(base_path))

    # 自动加载标准库 std/*.ku
    std_path = DAO_HOME / "std"
    if std_path.exists():
        for f in sorted(std_path.glob("*.ku")):
            try:
                env.load(str(f))
            except Exception as e:
                print(f"[WARN] 加载 {f.name} 失败: {e}", file=sys.stderr)

    # ── 反汇编 ──
    if args.dis:
        from .compiler import load_bytecode, disassemble
        try:
            bytecode = load_bytecode(args.dis)
            print(disassemble(bytecode))
        except Exception as e:
            print(f"ku 错误: {e}", file=sys.stderr)
            traceback.print_exc()
        return

    # ── 编译 ──
    if args.compile:
        from .compiler import KuCompiler, save_bytecode
        if not args.file:
            print("ku 错误: --compile 需要 -f 指定源文件", file=sys.stderr)
            sys.exit(1)
        try:
            compiler = KuCompiler()
            bytecode = compiler.compile_file(args.file)
            output_path = args.output
            if not output_path:
                # 默认输出到同目录同名 .kub 文件
                output_path = str(Path(args.file).with_suffix(".kub"))
            save_bytecode(bytecode, output_path)
            print(f"ku: 已编译 {args.file} → {output_path}")
            print(f"    {len(bytecode['constants'])} 个常量, {len(bytecode['instructions'])} 条指令")
        except Exception as e:
            print(f"ku 错误: {e}", file=sys.stderr)
            traceback.print_exc()
        return

    # 从状态恢复
    if args.load:
        count = env.load_state(args.load)
        print(f"ku: 恢复了 {count} 个 thought")

    # 加载源码
    if args.file:
        thoughts = env.load(args.file)
        print(f"ku: 加载了 {len(thoughts)} 个 thought 从 {args.file}")
        # 编译并执行文件中的非-thought 代码
        try:
            from .compiler import compile_ku, run_bytecode
            with open(args.file, encoding="utf-8") as f:
                source = f.read()
            lines = source.split("\n")
            remaining = []
            i = 0
            while i < len(lines):
                stripped = lines[i].strip()
                if stripped.startswith("thought ") or stripped.startswith("思 "):
                    depth = 0
                    found = False
                    done = False
                    while i < len(lines) and not done:
                        for ch in lines[i]:
                            if ch == "{": depth += 1; found = True
                            elif ch == "}": depth -= 1
                        if found and depth == 0: done = True
                        i += 1
                    continue
                if stripped and not stripped.startswith("//") and not stripped.startswith(";;"):
                    remaining.append(lines[i])
                i += 1
            code = "\n".join(remaining).strip()
            if code:
                bc = compile_ku(code)
                run_bytecode(bc, env)
        except Exception as e:
            print(f"[WARN] 执行剩余代码失败: {e}", file=sys.stderr)

    # 加载目录（如果没有指定文件）
    if not args.file and not args.exec and not args.load and not args.daemon:
        if DAO_HOME.exists():
            env.load_dir(str(DAO_HOME))

    # ── Daemon REPL ──
    if args.daemon:
        import shlex
        print("ku daemon — 天书母语 REPL")
        print(f"  {len(Thought.registry)} thoughts loaded")
        print("  Commands: run, exec, py, save, load, stats, reset, help, exit")
        state_path = str(DAO_HOME / ".ku_state.json")
        while True:
            try:
                line = input("ku> ").strip()
            except (EOFError, KeyboardInterrupt):
                print()
                break
            if not line:
                continue
            parts = shlex.split(line)
            cmd = parts[0]
            cmd_args = parts[1:]
            try:
                if cmd == "exit" or cmd == "quit":
                    break
                elif cmd == "help":
                    print("  run <thought> [args...]  — 运行 thought")
                    print("  exec <expr>              — 执行表达式")
                    print("  py <thought>             — 转译 Python")
                    print("  save                     — 保存状态")
                    print("  load                     — 恢复状态")
                    print("  stats                    — 统计信息")
                    print("  reset                    — 重置运行时")
                    print("  list                     — 列出所有 thought")
                    print("  help                     — 帮助")
                    print("  exit                     — 退出")
                elif cmd == "run":
                    if not cmd_args:
                        print("用法: run <thought> [args...]")
                        continue
                    thought_name = cmd_args[0]
                    thought_parsed_args = []
                    for a in cmd_args[1:]:
                        try:
                            thought_parsed_args.append(json.loads(a))
                        except (json.JSONDecodeError, ValueError):
                            thought_parsed_args.append(a)
                    result = env.run(thought_name, thought_parsed_args or None)
                    print(f"=> {_simplify(result)}")
                elif cmd == "exec":
                    expr = " ".join(cmd_args)
                    ast = _parse_expr(expr)
                    t = env.get("__exec__")
                    if not t:
                        t = Thought("__exec__", [], Node.lit(None))
                    t.body = ast
                    result = t.call()
                    print(f"=> {_simplify(result)}")
                elif cmd == "py":
                    if cmd_args:
                        print(env.export_python([cmd_args[0]]))
                elif cmd == "save":
                    path = env.save_state()
                    print(f"已保存到 {path}")
                elif cmd == "load":
                    count = env.load_state(state_path)
                    print(f"恢复了 {count} 个 thought")
                elif cmd == "stats":
                    s = env.stats()
                    print(f"{s['thoughts']} thoughts, {s['executions']} 次执行")
                elif cmd == "reset":
                    env.reset()
                    print("已重置")
                elif cmd == "list":
                    for name in sorted(Thought.registry.keys()):
                        t = Thought.registry[name]
                        print(f"  {name}{'()' if t.params else ''} — {t.doc or t.meta.get('executions', 0)}次执行")
                else:
                    print(f"未知命令: {cmd} (输入 help 查看帮助)")
            except Exception as e:
                print(f"错误: {e}")
        return

    # 执行表达式
    if args.exec:
        try:
            ast = _parse_expr(args.exec)
            # 临时创建一个 thought 来执行
            t = env.get("__exec__")
            if not t:
                t = Thought("__exec__", [], Node.lit(None))
            t.body = ast
            result = t.call()
            print(f"=> {_simplify(result)}")
        except Exception as e:
            print(f"ku 错误: {e}", file=sys.stderr)
            traceback.print_exc()

    # 运行 thought
    if args.run:
        try:
            thought_args = []
            for a in args.args:
                try:
                    thought_args.append(json.loads(a))
                except (json.JSONDecodeError, ValueError):
                    thought_args.append(a)
            result = env.run(args.run, thought_args if thought_args else None)
            print(f"=> {_simplify(result)}")
        except Exception as e:
            print(f"ku 错误: {e}", file=sys.stderr)
            traceback.print_exc()

    # 转译 Python
    if args.py:
        print(env.export_python([args.py]))

    # 统计
    if args.stats:
        s = env.stats()
        print(f"ku: {s['thoughts']} thoughts, {s['executions']} 次执行, {s['log_entries']} 条日志")

    # 保存
    if args.save:
        path = env.save_state()
        print(f"ku: 状态已保存到 {path}")

    if args.reset:
        env.reset()
        print("ku: 已重置")


# ═══════════════════════════════════════════
#  Parser 注入：Python 实现 → Ku 可调用
# ═══════════════════════════════════════════

try:
    from .dao_parser import parse_tokens as _py_parse_tokens, parse_tokens_as_nodes as _py_parse_tokens_nodes, dict_to_node as _py_dict_to_node
except (ImportError, SystemError):
    try:
        from dao_parser import parse_tokens as _py_parse_tokens, parse_tokens_as_nodes as _py_parse_tokens_nodes, dict_to_node as _py_dict_to_node
    except ImportError:
        _py_parse_tokens = None
        _py_parse_tokens_nodes = None
        _py_dict_to_node = None


def _inject_parser_to_env(env):
    """将 Python parser 注入到 Ku 环境。"""
    if _py_parse_tokens is None:
        return

    def _ku_parse_tokens(tokens):
        return _py_parse_tokens(tokens)

    Thought("parse_tokens", ["tokens"], _ku_parse_tokens,
            doc="解析 token 列表为 AST（Python 实现）")

    def _ku_ast_str(node, indent=0):
        return _ast_to_str(node, indent)

    def _ast_to_str(node, indent=0):
        if not isinstance(node, dict):
            return " " * indent + repr(node)
        sp = " " * indent
        t = node.get("type", "?")
        v = node.get("value", "")
        ch = node.get("children", [])
        s = f"{sp}{t}"
        if v:
            s += f": {v}"
        for c in ch:
            s += "\n" + _ast_to_str(c, indent + 2)
        return s

    Thought("ast_str", ["node", "indent"], _ku_ast_str,
            doc="AST 转可读字符串")

    if _py_parse_tokens_nodes is not None:
        def _ku_parse_tokens_nodes(tokens):
            return _py_parse_tokens_nodes(tokens)
        Thought("parse_tokens_as_nodes", ["tokens"], _ku_parse_tokens_nodes,
                doc="解析 tokens 为 Node 对象 AST（直接给 compiler 用）")

    if _py_dict_to_node is not None:
        def _ku_dict_to_node(d):
            return _py_dict_to_node(d)
        Thought("dict_to_node", ["d"], _ku_dict_to_node,
                doc="dict AST 转 Node 对象")


# ═══════════════════════════════════════════
#  编译器集成：让 .ku 能调用编译器
# ═══════════════════════════════════════════

try:
    from .compiler import KuCompiler, KuVM, disassemble
except (ImportError, SystemError):
    try:
        from compiler import KuCompiler, KuVM, disassemble
    except ImportError:
        KuCompiler = None
        KuVM = None
        disassemble = None

if KuCompiler is not None:
    _ku_compiler = KuCompiler()
    _ku_vm = KuVM()

    def _inject_compiler_to_env(env):
        """将编译器注入到 Ku 环境"""
        env.set("_compiler", _ku_compiler)
        env.set("_vm", _ku_vm)

        Thought("compile", ["source"],
                Node.call(Node.ref("_compiler.compile"), [Node.ref("source")]),
                doc="编译 .ku 源码为字节码")

        Thought("compile_file", ["path"],
                Node.call(Node.ref("_compiler.compile_file"), [Node.ref("path")]),
                doc="编译 .ku 文件为字节码")

        Thought("execute_bytecode", ["bytecode"],
                Node.call(Node.ref("_vm.execute"), [Node.ref("bytecode")]),
                doc="执行字节码")

        Thought("compile_and_run", ["source"],
                Node.call(Node.ref("_vm.run"), [Node.ref("source")]),
                doc="编译并执行 .ku 源码")

        Thought("disassemble_bytecode", ["bytecode"],
                Node.call(Node.ref("_disassemble_impl"), [Node.ref("bytecode")]),
                doc="反汇编字节码，用于调试")

        Thought("save_bytecode", ["bytecode", "path"],
                Node.call(Node.ref("_save_bytecode_impl"), [Node.ref("bytecode"), Node.ref("path")]),
                doc="保存字节码到 .kub 文件")

        # 辅助函数（因为 disassemble 和 save 是模块级函数，不是方法）
        def _disassemble_impl(bc):
            return disassemble(bc)

        def _save_bytecode_impl(bc, path):
            import json
            with open(path, 'w') as f:
                json.dump(bc, f, indent=2)
            return f"saved to {path}"

        env.set("_disassemble_impl", _disassemble_impl)
        env.set("_save_bytecode_impl", _save_bytecode_impl)

    # 不打印 — MCP 模式下 stdout 是 JSON-RPC 通道
else:
    def _inject_compiler_to_env(env):
        pass



# ═══════════════════════════════════════════════════════════════
#  天书 Agent v2.0 架构模块
#  对齐 ARCHITECTURE_v2.md 设计
# ═══════════════════════════════════════════════════════════════

import json, os, time, sqlite3, hashlib
from pathlib import Path
from typing import Any, Optional, Callable
from enum import Enum, auto


# ─────────────────────────────────────────────────────────────
#  1. ReAct 循环引擎
# ─────────────────────────────────────────────────────────────

class ReactPhase(Enum):
    THINK = auto()
    ACT = auto()
    OBSERVE = auto()
    RE_PLAN = auto()


class ReactState:
    def __init__(self, goal, max_turns=50):
        self.goal = goal
        self.max_turns = max_turns
        self.current_turn = 0
        self.phase = ReactPhase.THINK
        self.thoughts = []
        self.actions = []
        self.observations = []
        self.completed = False
        self.result = None
        self.error = None
        self.task_queue = []
        self.completed_tasks = []

    def to_dict(self):
        return {
            "goal": self.goal, "turn": self.current_turn,
            "phase": self.phase.name, "completed": self.completed,
            "tasks_pending": len(self.task_queue),
            "tasks_done": len(self.completed_tasks),
        }


class ReactLoop:
    def __init__(self, goal, tools=None, max_turns=50,
                 on_think=None, on_act=None, on_observe=None, on_replan=None):
        self.state = ReactState(goal, max_turns)
        self.tools = tools or []
        self.on_think = on_think
        self.on_act = on_act
        self.on_observe = on_observe
        self.on_replan = on_replan

    def run(self):
        while self.state.current_turn < self.state.max_turns:
            self.state.current_turn += 1
            self.state.phase = ReactPhase.THINK
            thought = self._think()
            self.state.thoughts.append(thought)
            if self.on_think: self.on_think(thought)
            if self.state.completed: break
            self.state.phase = ReactPhase.ACT
            action = self._act(thought)
            self.state.actions.append(action)
            if self.on_act: self.on_act(action)
            self.state.phase = ReactPhase.OBSERVE
            obs = self._observe(action)
            self.state.observations.append(obs)
            if self.on_observe: self.on_observe(obs)
            self.state.phase = ReactPhase.RE_PLAN
            self._re_plan(obs)
        return self.state.result

    def _think(self):
        if not self.state.task_queue:
            self.state.completed = True
            return {"type": "finish", "message": "all tasks done"}
        return {"type": "plan", "task": self.state.task_queue[0]}

    def _act(self, thought):
        if thought.get("type") == "finish":
            return {"type": "none"}
        task = thought.get("task", {})
        for tool in self.tools:
            if tool.get("name") == task.get("type"):
                try:
                    result = tool["handler"](task)
                    return {"type": "tool_result", "tool": tool["name"], "result": result}
                except Exception as e:
                    return {"type": "tool_error", "tool": tool["name"], "error": str(e)}
        return {"type": "no_tool", "task": task}

    def _observe(self, action):
        if action.get("type") == "tool_error":
            return {"success": False, "error": action.get("error")}
        return {"success": True, "action": action}

    def _re_plan(self, obs):
        if self.state.task_queue:
            task = self.state.task_queue.pop(0)
            if obs.get("success"):
                self.state.completed_tasks.append(task)
            else:
                task["retries"] = task.get("retries", 0) + 1
                if task["retries"] < 3:
                    self.state.task_queue.append(task)
                else:
                    self.state.completed_tasks.append({**task, "failed": True})



# ─────────────────────────────────────────────────────────────
#  2. 任务规划器
# ─────────────────────────────────────────────────────────────

class TaskPriority(Enum):
    CRITICAL = 0
    HIGH = 1
    NORMAL = 2
    LOW = 3
    BACKGROUND = 4


class Task:
    def __init__(self, name, task_type="generic", priority=TaskPriority.NORMAL,
                 deps=None, data=None):
        self.name = name
        self.type = task_type
        self.priority = priority
        self.deps = deps or []
        self.data = data or {}
        self.status = "pending"
        self.result = None
        self.error = None
        self.created_at = time.time()
        self.started_at = None
        self.completed_at = None

    def to_dict(self):
        return {
            "name": self.name, "type": self.type,
            "priority": self.priority.value, "status": self.status,
            "deps": self.deps,
        }


class TaskPlanner:
    def __init__(self):
        self.tasks = {}
        self.dependency_graph = {}

    def add_task(self, task):
        self.tasks[task.name] = task
        self.dependency_graph[task.name] = set(task.deps)

    def get_ready_tasks(self):
        ready = []
        for name, task in self.tasks.items():
            if task.status != "pending":
                continue
            deps_met = all(
                self.tasks.get(d, Task(d)).status == "completed"
                for d in task.deps
            )
            if deps_met:
                ready.append(task)
        ready.sort(key=lambda t: t.priority.value)
        return ready

    def complete_task(self, name, result=None):
        if name in self.tasks:
            self.tasks[name].status = "completed"
            self.tasks[name].result = result
            self.tasks[name].completed_at = time.time()

    def fail_task(self, name, error=None):
        if name in self.tasks:
            self.tasks[name].status = "failed"
            self.tasks[name].error = error

    def get_progress(self):
        total = len(self.tasks)
        completed = sum(1 for t in self.tasks.values() if t.status == "completed")
        failed = sum(1 for t in self.tasks.values() if t.status == "failed")
        pending = total - completed - failed
        return {"total": total, "completed": completed, "failed": failed, "pending": pending}

    @staticmethod
    def decompose(goal):
        tasks = []
        goal_lower = goal.lower()
        if "重构" in goal_lower or "refactor" in goal_lower:
            tasks.append(Task("analyze_code", "code_analysis", TaskPriority.HIGH))
            tasks.append(Task("plan_changes", "planning", TaskPriority.HIGH, deps=["analyze_code"]))
            tasks.append(Task("implement", "coding", TaskPriority.NORMAL, deps=["plan_changes"]))
            tasks.append(Task("test", "testing", TaskPriority.NORMAL, deps=["implement"]))
            tasks.append(Task("review", "review", TaskPriority.LOW, deps=["test"]))
        elif "测试" in goal_lower or "test" in goal_lower:
            tasks.append(Task("find_tests", "code_analysis", TaskPriority.HIGH))
            tasks.append(Task("run_tests", "testing", TaskPriority.HIGH, deps=["find_tests"]))
            tasks.append(Task("fix_failures", "coding", TaskPriority.NORMAL, deps=["run_tests"]))
        else:
            tasks.append(Task("understand", "analysis", TaskPriority.HIGH))
            tasks.append(Task("execute", "execution", TaskPriority.NORMAL, deps=["understand"]))
        return tasks



# ─────────────────────────────────────────────────────────────
#  3. 上下文管理器
# ─────────────────────────────────────────────────────────────

class ContextManager:
    def __init__(self, max_tokens=100000, compression_threshold=0.5, target_ratio=0.2):
        self.max_tokens = max_tokens
        self.compression_threshold = compression_threshold
        self.target_ratio = target_ratio
        self.messages = []
        self.compressed_summary = ""
        self.token_count = 0

    def add_message(self, role, content):
        self.messages.append({"role": role, "content": content, "time": time.time()})
        self._update_token_count()
        if self.token_count > self.max_tokens * self.compression_threshold:
            self._compress()

    def get_context(self):
        ctx = []
        if self.compressed_summary:
            ctx.append({"role": "system", "content": "Summary: " + self.compressed_summary})
        ctx.extend(self.messages[-20:])
        return ctx

    def _update_token_count(self):
        self.token_count = sum(len(m.get("content", "")) for m in self.messages)

    def _compress(self):
        if len(self.messages) <= 10:
            return
        to_compress = self.messages[:-10]
        self.messages = self.messages[-10:]
        summary_parts = []
        for m in to_compress:
            role = m.get("role", "?")
            content = str(m.get("content", ""))[:100]
            summary_parts.append(role + ": " + content)
        self.compressed_summary += " | ".join(summary_parts)
        self._update_token_count()



# ─────────────────────────────────────────────────────────────
#  4. 记忆系统
# ─────────────────────────────────────────────────────────────

class MemoryType(Enum):
    SESSION = "session"
    LONG_TERM = "long_term"
    ENTITY = "entity"
    FACT = "fact"


class Memory:
    def __init__(self, key, value, memory_type=MemoryType.SESSION, meta=None):
        self.key = key
        self.value = value
        self.type = memory_type
        self.meta = meta or {}
        self.created_at = time.time()
        self.accessed_at = time.time()
        self.access_count = 0
        self.strength = 1.0

    def access(self):
        self.accessed_at = time.time()
        self.access_count += 1
        self.strength = min(1.0, self.strength + 0.01)
        return self.value

    def weaken(self, amount=0.1):
        self.strength = max(0.0, self.strength - amount)

    def to_dict(self):
        return {
            "key": self.key, "type": self.type.value,
            "strength": self.strength, "access_count": self.access_count,
            "created_at": self.created_at,
        }



class MemorySystem:
    def __init__(self, db_path=None):
        self.memories = {}
        self.db_path = db_path
        self._entity_index = {}
        self._fact_index = {}

    def store(self, key, value, memory_type=MemoryType.SESSION, meta=None):
        mem = Memory(key, value, memory_type, meta)
        self.memories[key] = mem
        if memory_type == MemoryType.ENTITY:
            self._entity_index[key] = mem
        elif memory_type == MemoryType.FACT:
            self._fact_index[key] = mem
        return mem

    def recall(self, key):
        mem = self.memories.get(key)
        if mem:
            return mem.access()
        return None

    def forget(self, key):
        self.memories.pop(key, None)
        self._entity_index.pop(key, None)
        self._fact_index.pop(key, None)

    def search(self, query, limit=10):
        results = []
        query_lower = str(query).lower()
        for key, mem in self.memories.items():
            score = 0
            if query_lower in str(key).lower():
                score += 2
            if query_lower in str(mem.value).lower():
                score += 1
            score *= mem.strength
            if score > 0:
                results.append((mem, score))
        results.sort(key=lambda x: x[1], reverse=True)
        return [m for m, s in results[:limit]]

    def link(self, key1, key2, relation="related"):
        mem1 = self.memories.get(key1)
        mem2 = self.memories.get(key2)
        if mem1 and mem2:
            links1 = mem1.meta.setdefault("links", {})
            links1[key2] = relation
            links2 = mem2.meta.setdefault("links", {})
            links2[key1] = relation

    def get_linked(self, key):
        mem = self.memories.get(key)
        if not mem:
            return []
        links = mem.meta.get("links", {})
        return [(k, v) for k, v in links.items() if k in self.memories]

    def strengthen(self, key):
        mem = self.memories.get(key)
        if mem:
            mem.strength = min(1.0, mem.strength + 0.2)

    def weaken_memory(self, key):
        mem = self.memories.get(key)
        if mem:
            mem.weaken()

    def get_stats(self):
        total = len(self.memories)
        return {
            "total": total,
            "entities": len(self._entity_index),
            "facts": len(self._fact_index),
            "avg_strength": sum(m.strength for m in self.memories.values()) / max(1, total),
        }



# ─────────────────────────────────────────────────────────────
#  5. 工具系统（权限矩阵 + 安全执行）
# ─────────────────────────────────────────────────────────────

class ToolPermission(Enum):
    ALLOW = "allow"
    ASK = "ask"
    DENY = "deny"


class ToolRisk(Enum):
    SAFE = "safe"
    LOW = "low"
    MEDIUM = "medium"
    HIGH = "high"
    CRITICAL = "critical"


class Tool:
    def __init__(self, name, handler, description="",
                 permission=ToolPermission.ALLOW, risk=ToolRisk.SAFE,
                 dangerous_patterns=None):
        self.name = name
        self.handler = handler
        self.description = description
        self.permission = permission
        self.risk = risk
        self.dangerous_patterns = dangerous_patterns or []
        self.call_count = 0
        self.error_count = 0

    def call(self, args):
        for pattern in self.dangerous_patterns:
            if pattern in str(args):
                raise SecurityError("Dangerous pattern detected: " + pattern)
        self.call_count += 1
        try:
            return self.handler(args)
        except Exception as e:
            self.error_count += 1
            raise


class SecurityError(Exception):
    pass


class ToolSystem:
    def __init__(self):
        self.tools = {}
        self.permission_matrix = {}
        self.call_log = []

    def register(self, tool):
        self.tools[tool.name] = tool

    def set_permission(self, tool_name, permission):
        self.permission_matrix[tool_name] = permission

    def call(self, tool_name, args):
        tool = self.tools.get(tool_name)
        if not tool:
            raise NameError("Tool not found: " + tool_name)
        perm = self.permission_matrix.get(tool_name, tool.permission)
        if perm == ToolPermission.DENY:
            raise SecurityError("Tool denied: " + tool_name)
        result = tool.call(args)
        self.call_log.append({
            "tool": tool_name, "args": str(args)[:200],
            "time": time.time(), "success": True,
        })
        return result

    def get_tools_info(self):
        return {name: {"description": t.description, "risk": t.risk.value,
                       "calls": t.call_count, "errors": t.error_count}
                for name, t in self.tools.items()}



# ─────────────────────────────────────────────────────────────
#  6. 自纠正引擎
# ─────────────────────────────────────────────────────────────

class ReviewResult(Enum):
    PASS = "pass"
    FAIL = "fail"
    NEEDS_FIX = "needs_fix"


class SelfCorrectionEngine:
    def __init__(self, max_retries=3):
        self.max_retries = max_retries
        self.corrections = []

    def review(self, output, expected=None):
        issues = []
        if output is None:
            issues.append("Output is None")
        if isinstance(output, dict) and output.get("error"):
            issues.append("Error in output: " + str(output["error"]))
        if expected is not None and output != expected:
            issues.append("Output does not match expected")
        if issues:
            return ReviewResult.FAIL, issues
        return ReviewResult.PASS, []

    def self_fix(self, thought, error, env):
        fix_thought = Thought(
            name="_auto_fix_" + str(int(time.time())),
            params=["original", "error"],
            body=Node.block([
                Node("return", "", [Node.lit("fixed")]),
            ]),
            doc="Auto-generated fix for: " + str(error)[:100],
        )
        self.corrections.append({
            "original": thought.name,
            "error": str(error),
            "fix": fix_thought.name,
            "time": time.time(),
        })
        return fix_thought



# ─────────────────────────────────────────────────────────────
#  7. 流式 LLM 适配层
# ─────────────────────────────────────────────────────────────

class LLMProvider:
    def __init__(self, name, base_url, api_key, model, priority=0):
        self.name = name
        self.base_url = base_url
        self.api_key = api_key
        self.model = model
        self.priority = priority
        self.is_available = True
        self.last_error = None
        self.cooldown_until = 0

    def is_ready(self):
        return self.is_available and time.time() > self.cooldown_until


class LLMAdapter:
    def __init__(self):
        self.providers = []
        self.current_provider = None
        self.failover_count = 0

    def add_provider(self, provider):
        self.providers.append(provider)
        self.providers.sort(key=lambda p: p.priority)

    def get_current(self):
        if self.current_provider and self.current_provider.is_ready():
            return self.current_provider
        for p in self.providers:
            if p.is_ready():
                self.current_provider = p
                return p
        return None

    def failover(self):
        if self.current_provider:
            self.current_provider.is_available = False
            self.current_provider.cooldown_until = time.time() + 30
        self.failover_count += 1
        return self.get_current()


if __name__ == "__main__":
    main()

# v2 test
