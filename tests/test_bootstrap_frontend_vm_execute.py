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


def normalize_tokens(tokens):
    return [
        {"type": token.get("type", ""), "value": token.get("value", ""), "line": token.get("line", 0)}
        for token in tokens
    ]


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


def run_vm_frontend_parse(source):
    lexed = run_vm_call("lex", source)
    return run_vm_call("parse_tokens", lexed)


@pytest.fixture(scope="module", autouse=True)
def load_frontend_into_vm():
    Thought.registry.clear()
    env = DaoEnv()
    env.load(str(ROOT / "dao" / "std" / "compiler.ku"))

    ast = {"type": "block", "value": "", "children": []}
    for module_name in ("lexer.ku", "parser.ku"):
        ast["children"].extend(parse_file(ROOT / "dao" / "std" / module_name)["children"])

    bytecode = Thought.registry["compile_ast"].call([ast])
    DaoVM().execute(bytecode)


def test_vm_executes_ku_lexer_entrypoint():
    source = '// comment\nx = "dao"'

    vm_tokens = run_vm_call("lex", source)
    py_tokens = py_lex(source)

    assert normalize_tokens(vm_tokens) == normalize_tokens(py_tokens)


def test_vm_executes_ku_frontend_pipeline_for_small_program():
    source = "思 add(a, b) { 返 +(a, b) }\nadd(1, 2)"

    vm_ast = run_vm_frontend_parse(source)
    py_ast = py_parse_tokens(py_lex(source))

    assert normalize(vm_ast) == normalize(py_ast)


def test_vm_executes_ku_frontend_pipeline_for_semantic_std_sources():
    for module_name in ("semantic_core.ku", "memory_env.ku", "patch.ku", "trace.ku"):
        source = (ROOT / "dao" / "std" / module_name).read_text(encoding="utf-8")

        vm_ast = run_vm_frontend_parse(source)
        py_ast = py_parse_tokens(py_lex(source))

        assert normalize(vm_ast) == normalize(py_ast)
