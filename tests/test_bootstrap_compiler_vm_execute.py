from pathlib import Path

import pytest

from dao.compiler import DaoVM
from dao.dao_lexer import lex as py_lex
from dao.dao_parser import parse_tokens as py_parse_tokens
from dao.runtime import DaoEnv, Thought


ROOT = Path(__file__).resolve().parents[1]


def parse_file(path):
    tokens = py_lex(path.read_text(encoding="utf-8"))
    tokens.append({"type": "eof", "value": "", "line": 0, "col": 0, "pos": 0})
    return py_parse_tokens(tokens)


def lit(value):
    return {"type": "literal", "value": value}


def ref(name):
    return {"type": "ref", "value": name}


def block(*children):
    return {"type": "block", "children": list(children)}


def call(name, *args):
    return {"type": "call", "value": name, "children": list(args)}


def thought(name, params, body):
    return {"type": "thought", "value": name, "children": list(params) + [body]}


def dict_node(**items):
    return {
        "type": "dict",
        "children": [
            {"type": "pair", "value": key, "children": [value]}
            for key, value in items.items()
        ],
    }


def normalize(node):
    if isinstance(node, dict):
        result = {"type": node.get("type", ""), "value": node.get("value", "")}
        children = node.get("children", [])
        if children:
            result["children"] = [normalize(child) for child in children]
        return result
    if isinstance(node, list):
        return [normalize(item) for item in node]
    return node


def run_vm_call(name, *constants):
    instructions = [["LOAD_NAME", name]]
    for index, _ in enumerate(constants):
        instructions.append(["LOAD_CONST", index])
    instructions.extend([["CALL", len(constants)], ["RETURN"]])
    return DaoVM().execute(
        {
            "format": "kub",
            "version": "0.1",
            "constants": list(constants),
            "instructions": instructions,
            "entry": 0,
        }
    )


def load_frontend_modules_with_vm_compiler():
    frontend_ast = {"type": "block", "value": "", "children": []}
    for module_name in ("lexer.ku", "parser.ku"):
        frontend_ast["children"].extend(parse_file(ROOT / "dao" / "std" / module_name)["children"])

    frontend_bytecode = run_vm_call("compile_ast", frontend_ast)
    DaoVM().execute(frontend_bytecode)
    return frontend_bytecode


COMPILER_CASES = [
    (
        "implicit_dict_return_thought",
        block(
            thought("wrap", ["x"], block(dict_node(value=ref("x")))),
            call("wrap", lit(7)),
        ),
        {"value": 7},
    ),
    (
        "implicit_literal_return_thought",
        block(
            thought("one", [], block(lit(1))),
            call("one"),
        ),
        1,
    ),
]


@pytest.fixture(scope="module")
def bootstrap_expected_bytecode():
    Thought.registry.clear()
    env = DaoEnv()
    env.load(str(ROOT / "dao" / "std" / "compiler.ku"))

    compiler_ast = parse_file(ROOT / "dao" / "std" / "compiler.ku")
    compiler_bytecode = Thought.registry["compile_ast"].call([compiler_ast])

    expected = {
        name: Thought.registry["compile_ast"].call([ast])
        for name, ast, _ in COMPILER_CASES
    }

    DaoVM().execute(compiler_bytecode)
    return expected


@pytest.mark.parametrize(("name", "ast", "expected_result"), COMPILER_CASES, ids=[case[0] for case in COMPILER_CASES])
def test_vm_executes_ku_compiler_entrypoint(bootstrap_expected_bytecode, name, ast, expected_result):
    vm_bytecode = run_vm_call("compile_ast", ast)

    assert vm_bytecode == bootstrap_expected_bytecode[name]
    assert DaoVM().execute(vm_bytecode) == expected_result


def test_vm_compiler_builds_frontend_modules(bootstrap_expected_bytecode):
    load_frontend_modules_with_vm_compiler()

    source = "思 add(a, b) { 返 +(a, b) }\nadd(1, 2)"
    vm_tokens = run_vm_call("lex", source)
    vm_ast = run_vm_call("parse_tokens", vm_tokens)
    py_ast = py_parse_tokens(py_lex(source))

    assert normalize(vm_ast) == normalize(py_ast)


def test_vm_frontend_ast_feeds_vm_compiler(bootstrap_expected_bytecode):
    load_frontend_modules_with_vm_compiler()

    source = "思 add(a, b) { 返 +(a, b) }\nadd(1, 2)"
    vm_tokens = run_vm_call("lex", source)
    vm_ast = run_vm_call("parse_tokens", vm_tokens)
    vm_bytecode = run_vm_call("compile_ast", vm_ast)

    assert DaoVM().execute(vm_bytecode) == 3
