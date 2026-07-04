"""
ku_parser.py — Python 实现的 Ku parser
自举第一步：用 Python 注入 parser，让 lexer→parser→compiler 链条跑通。
后续用这个 parser 去解析 .ku 重写的 parser（经典自举路径）。
"""

def _res(node, pos):
    return {"n": node, "p": pos}

def _node(type, value, children=None):
    return {"type": type, "value": value, "children": children or []}

def _lit(val):
    return _node("literal", val, [])

def _ref(name):
    return _node("ref", name, [])

def _op(op, left, right):
    return _node("op", op, [left, right])

def _call(name, args):
    return _node("call", name, args)

def _assign(name, val):
    return _node("assign", name, [val])

def _block(stmts):
    return _node("block", "", stmts)

def _list(items):
    return _node("list", "", items)

def _dict(pairs):
    return _node("dict", "", pairs)

def _index(obj, idx):
    return _node("index", "", [obj, idx])

def _ret(val):
    return _node("return", "", [val])

def _ifnode(cond, then_b, else_b=None):
    children = [cond, then_b]
    if else_b is not None:
        children.append(else_b)
    return _node("if", "", children)

def _whilenode(cond, body):
    return _node("while", "", [cond, body])

def _fornode(var, iterable, body):
    return _node("for", var, [iterable, body])

_OP_BP = {
    "|>": 0,
    "or": 1, "and": 2,
    "==": 3, "!=": 3,
    "<": 4, ">": 4, "<=": 4, ">=": 4,
    "+": 5, "-": 5,
    "*": 6, "/": 6, "%": 6,
}

def parse_tokens(tokens):
    """主入口：token 列表 → AST"""
    pos = 0
    end = len(tokens)
    stmts = []
    while pos < end:
        t = tokens[pos]
        if t["type"] == "newline":
            pos += 1
            continue
        if t["type"] == "eof":
            break
        node, pos = _stmt(tokens, pos, end)
        stmts.append(node)
    return desugar_pipe(_block(stmts))


def _has_placeholder(node):
    """检查 AST 中是否包含 _ 占位符。"""
    if not isinstance(node, dict):
        return False
    if node.get("type") == "ref" and node.get("value") == "_":
        return True
    return any(_has_placeholder(c) for c in node.get("children", []) if isinstance(c, dict))


def _replace_placeholder(node, replacement):
    """将 AST 中的 _ 替换为 replacement。"""
    if not isinstance(node, dict):
        return node
    if node.get("type") == "ref" and node.get("value") == "_":
        return replacement
    children = node.get("children", [])
    new_children = [_replace_placeholder(c, replacement) if isinstance(c, dict) else c
                    for c in children]
    node = dict(node)
    node["children"] = new_children
    return node


def desugar_pipe(node):
    """递归遍历 AST，将 |> 节点脱糖为 call。AST 保持纯 dict。"""
    if not isinstance(node, dict):
        return node
    children = node.get("children", [])
    if isinstance(children, list):
        children = [desugar_pipe(c) for c in children]
    node = dict(node)
    node["children"] = children
    if node["type"] == "op" and node["value"] == "|>" and len(children) == 2:
        left = children[0]
        right = children[1]
        # _ 占位符：x |> f(_, 10) → f(x, 10)
        if _has_placeholder(right):
            replaced = _replace_placeholder(right, left)
            return replaced
        # 保留 value 给 dict_to_node 处理 callee ref 插入
        if right["type"] == "call" and right["value"]:
            return _call(right["value"], [left] + right["children"])
        elif right["type"] == "call" and not right["value"]:
            return _node("call", "", [right["children"][0], left] + right["children"][1:])
        elif right["type"] == "ref":
            return _call(right["value"], [left])
        else:
            return _node("call", "", [right, left])
    return node


def _peek(tokens, pos):
    if pos < len(tokens):
        return tokens[pos]
    return {"type": "eof", "value": ""}


def _stmt(tokens, pos, end):
    t = _peek(tokens, pos)

    # EOF — 不能前进，返回空节点防止无限循环
    if t["type"] == "eof":
        return _lit(""), pos

    # thought definition
    if t["type"] == "keyword" and t["value"] in ("thought", "思"):
        return _parse_thought(tokens, pos + 1, end)

    # if
    if t["type"] == "keyword" and t["value"] in ("if", "若"):
        return _parse_if(tokens, pos + 1, end)

    # while
    if t["type"] == "keyword" and t["value"] in ("while", "当"):
        return _parse_while(tokens, pos + 1, end)

    # for
    if t["type"] == "keyword" and t["value"] in ("for", "遍"):
        return _parse_for(tokens, pos + 1, end)

    # return
    if t["type"] == "keyword" and t["value"] in ("return", "返"):
        return _parse_return(tokens, pos + 1, end)

    # break / continue
    if t["type"] == "keyword" and t["value"] in ("break", "断"):
        return _node("break", "", []), pos + 1
    if t["type"] == "keyword" and t["value"] in ("continue", "续"):
        return _node("continue", "", []), pos + 1

    # try/catch
    if t["type"] == "keyword" and t["value"] in ("try", "试"):
        return _parse_try(tokens, pos + 1, end)

    # throw
    if t["type"] == "keyword" and t["value"] in ("throw", "抛"):
        val, new_pos = _expr(tokens, pos + 1, end)
        return _node("throw", "", [val]), new_pos

    # assignment: name = expr
    if t["type"] == "name" and pos + 2 < end:
        t2 = tokens[pos + 1]
        if t2["type"] == "op" and t2["value"] == "=":
            val, new_pos = _expr(tokens, pos + 2, end)
            return _assign(t["value"], val), new_pos
        # index assign: name[idx] = expr
        if t2["value"] == "[":
            idx_end = _find_close(tokens, pos + 2, end, "[", "]")
            if idx_end + 1 < end and tokens[idx_end + 1]["value"] == "=":
                obj = _ref(t["value"])
                idx, _ = _expr(tokens, pos + 2, idx_end)
                val, val_end = _expr(tokens, idx_end + 2, end)
                return _node("index_assign", "", [_index(obj, idx), val]), val_end

    # expression statement — use _expr's own position tracking
    node, new_pos = _expr(tokens, pos, end)
    return node, new_pos


def _line_end(tokens, pos, end):
    while pos < end:
        t = tokens[pos]
        if t["type"] in ("newline", "eof"):
            return pos
        pos += 1
    return pos


def _skip(tokens, pos, typ, val):
    t = _peek(tokens, pos)
    if t["type"] == typ and t["value"] == val:
        return pos + 1
    return pos


# ── thought ──
def _parse_thought(tokens, pos, end):
    name = _peek(tokens, pos)["value"]
    pos += 1
    pos = _skip(tokens, pos, "punct", "(")
    params = []
    while pos < end:
        t = _peek(tokens, pos)
        if t["value"] == ")":
            break
        if t["type"] == "name":
            params.append(t["value"])
            pos += 1
            if _peek(tokens, pos)["value"] == ",":
                pos += 1
        else:
            break
    pos = _skip(tokens, pos, "punct", ")")
    body, pos = _parse_brace(tokens, pos, end)
    return _node("thought", name, params + [body]), pos


# ── { } block ──
def _parse_brace(tokens, pos, end):
    pos = _skip(tokens, pos, "punct", "{")
    stmts = []
    while pos < end:
        t = _peek(tokens, pos)
        if t["value"] == "}" or t["type"] == "eof":
            return _block(stmts), pos + 1
        if t["type"] == "newline":
            pos += 1
            continue
        node, pos = _stmt(tokens, pos, end)
        stmts.append(node)
    return _block(stmts), pos


# ── if ──
def _parse_if(tokens, pos, end):
    # find { for condition boundary
    brace_pos = _find_brace(tokens, pos, end)
    cond, _ = _expr(tokens, pos, brace_pos)
    then_b, pos = _parse_brace(tokens, brace_pos, end)
    close_tok = _peek(tokens, pos - 1)
    else_b = None

    # explicit else may start on the next line
    else_pos = pos
    while else_pos < end and _peek(tokens, else_pos)["type"] == "newline":
        else_pos += 1
    t = _peek(tokens, else_pos)
    if t["type"] == "keyword" and t["value"] == "else":
        pos = else_pos + 1  # skip 'else'
        while pos < end and _peek(tokens, pos)["type"] == "newline":
            pos += 1
        t2 = _peek(tokens, pos)
        if t2["type"] == "keyword" and t2["value"] == "if":
            # else if ... { ... } — 递归解析为嵌套 if
            else_b, pos = _parse_if(tokens, pos + 1, end)
        elif t2["value"] == "{":
            else_b, pos = _parse_brace(tokens, pos, end)
    elif t["value"] == "{":
        # implicit else is only the compact form: } { ... }
        if t.get("line") == close_tok.get("line"):
            else_b, pos = _parse_brace(tokens, pos, end)
    return _ifnode(cond, then_b, else_b), pos


def _parse_if_expr(tokens, pos, end):
    """解析 if 表达式（赋值右侧）: if (cond) { then } { else }"""
    # skip newlines
    while pos < end and _peek(tokens, pos)["type"] == "newline":
        pos += 1
    # expect (
    pos = _skip(tokens, pos, "punct", "(")
    # parse condition
    close = _find_close(tokens, pos, end, "(", ")")
    cond, _ = _expr(tokens, pos, close)
    pos = close + 1
    # then block
    then_b, pos = _parse_brace(tokens, pos, end)
    # else block (implicit: } {)
    else_b = None
    while pos < end and _peek(tokens, pos)["type"] == "newline":
        pos += 1
    if pos < end and _peek(tokens, pos)["value"] == "{":
        else_b, pos = _parse_brace(tokens, pos, end)
    return _ifnode(cond, then_b, else_b), pos


# ── while ──
def _parse_while(tokens, pos, end):
    brace_pos = _find_brace(tokens, pos, end)
    cond, _ = _expr(tokens, pos, brace_pos)
    body, pos = _parse_brace(tokens, brace_pos, end)
    return _whilenode(cond, body), pos


# ── for ──
def _parse_for(tokens, pos, end):
    var = _peek(tokens, pos)["value"]
    pos += 1
    pos = _skip(tokens, pos, "keyword", "in")
    pos = _skip(tokens, pos, "keyword", "于")
    brace_pos = _find_brace(tokens, pos, end)
    iterable, _ = _expr(tokens, pos, brace_pos)
    body, pos = _parse_brace(tokens, brace_pos, end)
    return _fornode(var, iterable, body), pos


# ── return ──
def _parse_return(tokens, pos, end):
    t = _peek(tokens, pos)
    if t["type"] in ("newline", "eof"):
        return _ret(_lit("")), pos
    val, new_pos = _expr(tokens, pos, end)
    return _ret(val), new_pos


# ── try/catch ──
def _parse_try(tokens, pos, end):
    try_body, pos = _parse_brace(tokens, pos, end)
    t = _peek(tokens, pos)
    if t["type"] == "keyword" and t["value"] in ("catch", "捕"):
        pos += 1
        catch_var = _peek(tokens, pos)["value"] if _peek(tokens, pos)["type"] == "name" else ""
        if _peek(tokens, pos)["type"] == "name":
            pos += 1
        catch_body, pos = _parse_brace(tokens, pos, end)
        return _node("try", "", [try_body, _ref(catch_var), catch_body]), pos
    return _node("try", "", [try_body]), pos


# ── helpers ──
def _find_brace(tokens, pos, end):
    while pos < end:
        t = _peek(tokens, pos)
        if t["type"] == "punct" and t["value"] == "{":
            return pos
        pos += 1
    return pos


def _find_close(tokens, pos, end, open_ch, close_ch):
    depth = 1
    while pos < end and depth > 0:
        t = _peek(tokens, pos)
        if t["type"] == "punct" and t["value"] == open_ch:
            depth += 1
        if t["type"] == "punct" and t["value"] == close_ch:
            depth -= 1
        if depth > 0:
            pos += 1
    return pos


# ══════════════════════════════════════════
#  Expression parsing (Pratt parser)
# ══════════════════════════════════════════

def _expr(tokens, pos, end):
    """解析表达式，返回 (node, new_pos)"""
    return _bp(tokens, pos, end, 0)


def _bp(tokens, pos, end, min_bp):
    # skip newlines before prefix
    while pos < end and _peek(tokens, pos)["type"] == "newline":
        pos += 1
    left, pos = _prefix(tokens, pos, end)

    while pos < end:
        # skip newlines before infix operator / call / index
        while pos < end and _peek(tokens, pos)["type"] == "newline":
            pos += 1
        t = _peek(tokens, pos)

        # function call (args...)
        if t["value"] == "(":
            args, pos = _parse_call_args(tokens, pos, end)
            if isinstance(left, dict) and left.get("type") == "ref":
                left = _call(left["value"], args)
            else:
                left = _call("", [left] + args)
            continue

        # index [expr]
        if t["value"] == "[":
            pos += 1
            idx_end = _find_close(tokens, pos, end, "[", "]")
            idx, _ = _expr(tokens, pos, idx_end)
            left = _index(left, idx)
            pos = idx_end + 1
            continue

        # attribute access .attr
        if t["value"] == ".":
            pos += 1
            attr_name = _peek(tokens, pos)["value"]
            pos += 1
            left = _node("attr", attr_name, [left])
            continue

        # lambda: name -> expr  or  (a, b) -> expr
        if t["type"] == "op" and t["value"] == "->":
            if left.get("type") == "ref":
                params = [left["value"]]
            elif left.get("type") == "list":
                params = [c["value"] for c in left.get("children", [])]
            else:
                params = ["_"]
            body, pos = _bp(tokens, pos + 1, end, 0)
            left = _node("lambda", "", [_block([_ret(body)])] + [_lit(p) for p in params])
            continue

        # binary operator (exclude assignment — handled at statement level)
        if t["type"] == "op" and t["value"] != "=":
            bp = _OP_BP.get(t["value"], 0)
            if bp < min_bp:
                break
            op = t["value"]
            right, pos = _bp(tokens, pos + 1, end, bp + 1)
            left = _op(op, left, right)
            continue

        break

    return left, pos


def _prefix(tokens, pos, end):
    # skip newlines
    while pos < end and _peek(tokens, pos)["type"] == "newline":
        pos += 1
    t = _peek(tokens, pos)

    # number
    if t["type"] == "number":
        val = t["value"]
        if "." in val:
            return _lit(float(val)), pos + 1
        return _lit(int(val)), pos + 1

    # string
    if t["type"] == "string":
        return _lit(t["value"]), pos + 1

    # true/false/null
    if t["type"] == "keyword":
        if t["value"] in ("true", "真"):
            return _lit(True), pos + 1
        if t["value"] in ("false", "假"):
            return _lit(False), pos + 1
        if t["value"] in ("null", "空"):
            return _lit(""), pos + 1
        # and/or/not used as function calls: and(a, b) / 且(a, b)
        if t["value"] in ("and", "or", "not", "且", "或", "非") and pos + 1 < end:
            next_t = _peek(tokens, pos + 1)
            if next_t["value"] == "(":
                args, new_pos = _parse_call_args(tokens, pos + 1, end)
                return _call(t["value"], args), new_pos

        # if expression: if (cond) { then } { else } / 若 (cond) { then } { else }
        if t["value"] in ("if", "若"):
            return _parse_if_expr(tokens, pos + 1, end)

    # name
    if t["type"] == "name":
        return _ref(t["value"]), pos + 1

    # operator as function name: + (a, b) → call("+", [a, b])
    if t["type"] == "op" and pos + 1 < end:
        next_t = _peek(tokens, pos + 1)
        if next_t["value"] == "(":
            args, new_pos = _parse_call_args(tokens, pos + 1, end)
            return _call(t["value"], args), new_pos

    # (group)
    if t["value"] == "(":
        pos += 1
        close = _find_close(tokens, pos, end, "(", ")")
        # 多参数 lambda：(a, b) -> ...
        if close + 1 < end and _peek(tokens, close + 1)["type"] == "op" and _peek(tokens, close + 1)["value"] == "->":
            params = []
            p = pos
            while p < close:
                tk = _peek(tokens, p)
                if tk["type"] == "name":
                    params.append(_lit(tk["value"]))
                    p += 1
                elif tk["value"] == ",":
                    p += 1
                else:
                    p += 1
            return _list(params), close + 1
        node, _ = _expr(tokens, pos, close)
        return node, close + 1

    # [list]
    if t["value"] == "[":
        return _parse_list(tokens, pos, end)

    # {dict}
    if t["value"] == "{":
        return _parse_dict(tokens, pos, end)

    # prefix minus
    if t["value"] == "-":
        right, pos = _prefix(tokens, pos + 1, end)
        return _op("-", _lit(0), right), pos

    # prefix not
    if t["type"] == "op" and t["value"] == "not":
        right, pos = _prefix(tokens, pos + 1, end)
        return _op("not", right, _lit("")), pos

    # unknown
    return _lit(""), pos + 1


def _parse_call_args(tokens, pos, end):
    pos += 1  # skip (
    args = []
    arg_start = pos
    depth = 1
    bracket_depth = 0  # track [ ] nesting
    while pos < end and depth > 0:
        t = tokens[pos]
        if t["type"] == "punct":
            if t["value"] == "(":
                depth += 1
            if t["value"] == ")":
                depth -= 1
                if depth == 0:
                    if pos > arg_start:
                        arg, _ = _expr(tokens, arg_start, pos)
                        args.append(arg)
                    break
            if t["value"] == "[":
                bracket_depth += 1
            if t["value"] == "]":
                bracket_depth -= 1
            if t["value"] == "," and depth == 1 and bracket_depth == 0:
                arg, _ = _expr(tokens, arg_start, pos)
                args.append(arg)
                arg_start = pos + 1
        pos += 1
    return args, pos + 1


def _parse_list(tokens, pos, end):
    pos += 1  # skip [
    items = []
    item_start = pos
    depth = 1
    while pos < end and depth > 0:
        t = tokens[pos]
        if t["value"] == "[":
            depth += 1
        if t["value"] == "]":
            depth -= 1
            if depth == 0:
                if pos > item_start:
                    item, _ = _expr(tokens, item_start, pos)
                    items.append(item)
                break
        if t["value"] == "," and depth == 1:
            item, _ = _expr(tokens, item_start, pos)
            items.append(item)
            item_start = pos + 1
        pos += 1
    return _list(items), pos + 1


def _parse_dict(tokens, pos, end):
    pos += 1  # skip {
    pairs = []
    depth = 1
    paren_depth = 0
    bracket_depth = 0
    key_start = pos
    state = "key"
    cur_key = ""
    while pos < end and depth > 0:
        t = tokens[pos]
        if t["value"] == "{":
            depth += 1
        if t["value"] == "}":
            depth -= 1
            if depth == 0:
                if state == "val" and pos > key_start:
                    val, _ = _expr(tokens, key_start, pos)
                    pairs.append(_node("pair", cur_key, [val]))
                break
        if t["value"] == "(":
            paren_depth += 1
        if t["value"] == ")":
            paren_depth -= 1
        if t["value"] == "[":
            bracket_depth += 1
        if t["value"] == "]":
            bracket_depth -= 1
        if t["value"] == ":" and depth == 1 and state == "key":
            cur_key = _get_key(tokens, key_start, pos)
            key_start = pos + 1
            state = "val"
        if t["value"] == "," and depth == 1 and paren_depth == 0 and bracket_depth == 0 and state == "val":
            val, _ = _expr(tokens, key_start, pos)
            pairs.append(_node("pair", cur_key, [val]))
            key_start = pos + 1
            state = "key"
        pos += 1
    return _dict(pairs), pos + 1


def _get_key(tokens, fr, to):
    # 跳过 newline 找真正的 key
    while fr < to and tokens[fr]["type"] == "newline":
        fr += 1
    if fr < to:
        t = tokens[fr]
        if t["type"] == "string":
            return t["value"]
        if t["type"] == "name":
            return t["value"]
    return ""


_OP_NAMES = {"+", "-", "*", "/", "%", "==", "!=", "<", ">", "<=", ">=",
             "and", "or", "not"}


def dict_to_node(d):
    """将 dict 格式的 AST 转换为 Node 对象（兼容 compiler）。"""
    try:
        from .runtime import Node
    except (ImportError, SystemError):
        from runtime import Node
    if not isinstance(d, dict):
        # 非 dict 元素（如 thought 的参数名字符串），包装为 literal
        return Node("literal", d, [])
    children = [dict_to_node(c) for c in d.get("children", [])]
    n = Node(d["type"], d["value"], children)
    # 运算符风格函数调用：+(a,b) → op(+, a, b)
    if n.type == "call" and n.value in _OP_NAMES:
        if len(n.children) == 1:
            return Node.op(n.value, n.children[0])
        elif len(n.children) == 2:
            return Node.op(n.value, n.children[0], n.children[1])
    # Pratt parser: _call("f", [args]) → value="f", children=[args]
    # Runtime 期望: children=[ref("f"), args]，value 不用
    if n.type == "call" and n.value:
        callee_ref = Node("ref", n.value, [])
        n.children.insert(0, callee_ref)
        n.value = ""
    return n


def parse_tokens_as_nodes(tokens):
    """解析 tokens 并返回 Node 对象格式的 AST（直接给 compiler 用）。"""
    ast_dict = parse_tokens(tokens)
    return dict_to_node(ast_dict)
