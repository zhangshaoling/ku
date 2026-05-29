"""Test: Ku parser vs Python parser — AST comparison for bootstrap verification."""

import sys
import json
import os

# Add parent dir to path so `ku` package is importable
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from ku.ku_lexer import lex
from ku.ku_parser import parse_tokens as py_parse
from ku.runtime import KuEnv, Thought

TESTS = {
    "basic_assign": 'x = 42',
    "string": 'name = "hello"',
    "float_num": 'pi = 3.14',
    "bool_null": 'a = true\nb = false\nc = null',
    "arithmetic": 'result = + (3, 4)',
    "comparison": 'ok = > (x, 0)',
    "if_else": 'if (x > 0) {\n  print("pos")\n} {\n  print("neg")\n}',
    "while_loop": 'i = 0\nwhile (< (i, 10)) {\n  i = + (i, 1)\n}',
    "for_loop": 'for item in items {\n  print(item)\n}',
    "function_def": 'thought add(a, b) {\n  return(+ (a, b))\n}',
    "pipe_basic": 'x |> print',
    "pipe_call": 'x |> f(a)',
    "pipe_placeholder": 'x |> f(_, 10)',
    "lambda_single": 'fn = x -> + (x, 1)',
    "lambda_multi": 'fn = (a, b) -> + (a, b)',
    "attr_access": 'obj.name',
    "index_access": 'arr[0]',
    "list_literal": 'items = [1, 2, 3]',
    "dict_literal": 'cfg = {"host": "localhost"}',
    "try_catch": 'try {\n  risky()\n} catch e {\n  print(e)\n}',
    "func_call": 'print("hello")',
    "not_prefix": 'x = not (a)',
}


def normalize_ast(node):
    if not isinstance(node, dict):
        return node
    result = {"type": node.get("type", ""), "value": node.get("value", "")}
    children = node.get("children", [])
    if children:
        result["children"] = [normalize_ast(c) for c in children]
    return result


def run_ku_parser(tokens):
    std_dir = os.path.join(os.path.dirname(__file__), "..", "ku", "std")
    env = KuEnv()
    env.load(os.path.join(std_dir, "lexer.ku"))
    env.load(os.path.join(std_dir, "parser.ku"))

    parse_fn = Thought.registry.get("parse_tokens")
    if parse_fn is None:
        raise RuntimeError("parse_tokens not found in registry")

    result = parse_fn.call(args=[tokens])
    return result


def test_single(name, source):
    tokens = lex(source)
    py_ast = py_parse(tokens)

    try:
        ku_ast = run_ku_parser(tokens)
    except Exception as e:
        return f"FAIL [{name}]: Ku error: {e}", False

    py_norm = normalize_ast(py_ast)
    ku_norm = normalize_ast(ku_ast)

    if py_norm == ku_norm:
        return f"OK   [{name}]", True
    else:
        py_s = json.dumps(py_norm, indent=2)[:300]
        ku_s = json.dumps(ku_norm, indent=2)[:300]
        return f"FAIL [{name}]:\n  PY: {py_s}\n  KU: {ku_s}", False


if __name__ == "__main__":
    print("=== Ku Parser Bootstrap Test ===\n")
    passed = 0
    failed = 0
    errors = []
    for name, source in TESTS.items():
        try:
            msg, ok = test_single(name, source)
        except Exception as e:
            msg = f"ERR  [{name}]: {e}"
            ok = False
        print(msg)
        if ok:
            passed += 1
        else:
            failed += 1
            errors.append(name)

    print(f"\n=== Results: {passed} passed, {failed} failed ===")
    if errors:
        print(f"Failed: {', '.join(errors)}")
    sys.exit(0 if failed == 0 else 1)
