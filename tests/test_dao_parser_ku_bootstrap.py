from pathlib import Path

from dao.dao_lexer import lex as py_lex
from dao.dao_parser import parse_tokens as py_parse_tokens
from dao.runtime import DaoEnv, Thought


ROOT = Path(__file__).resolve().parents[1]


def load_ku_parser():
    Thought.registry.clear()
    env = DaoEnv()
    env.load(str(ROOT / "dao" / "std" / "lexer.ku"))
    env.load(str(ROOT / "dao" / "std" / "parser.ku"))


def ku_parse(source):
    tokens = Thought.registry["lex"].call([source])
    return Thought.registry["parse_tokens"].call([tokens])


def normalize(node):
    if isinstance(node, dict):
        result = {"type": node.get("type", ""), "value": node.get("value", "")}
        children = node.get("children", [])
        if children:
            result["children"] = [normalize(child) for child in children]
        return result
    return node


def test_ku_lexer_skips_slash_comments_before_parsing():
    load_ku_parser()
    source = "// generated demo source\nx = 1"

    ku_ast = ku_parse(source)
    py_ast = py_parse_tokens(py_lex(source))

    assert normalize(ku_ast) == normalize(py_ast)


def test_ku_parser_handles_string_index_assignment():
    load_ku_parser()
    source = 'x["a"] = y'

    ku_ast = ku_parse(source)
    py_ast = py_parse_tokens(py_lex(source))

    assert normalize(ku_ast) == normalize(py_ast)


def test_ku_parser_keeps_nested_call_commas_inside_containers():
    load_ku_parser()
    source = 'xs = [instr[0], +(load_ci, const_offset)]\nd = {"sum": +(left, right)}'

    ku_ast = ku_parse(source)
    py_ast = py_parse_tokens(py_lex(source))

    assert normalize(ku_ast) == normalize(py_ast)

    list_node = ku_ast["children"][0]["children"][0]
    assert len(list_node["children"]) == 2
    assert list_node["children"][1]["type"] == "call"
    assert list_node["children"][1]["value"] == "+"
    assert len(list_node["children"][1]["children"]) == 2

    dict_value = ku_ast["children"][1]["children"][0]["children"][0]["children"][0]
    assert dict_value["type"] == "call"
    assert dict_value["value"] == "+"
    assert len(dict_value["children"]) == 2


def test_ku_parser_can_parse_semantic_std_sources():
    load_ku_parser()

    for module_name in ("semantic_core.ku", "memory_env.ku", "patch.ku", "trace.ku"):
        ast = ku_parse((ROOT / "dao" / "std" / module_name).read_text(encoding="utf-8"))

        assert ast["type"] == "block"
        assert ast["children"]


def test_ku_parser_matches_python_parser_for_semantic_std_sources():
    load_ku_parser()

    for module_name in ("semantic_core.ku", "memory_env.ku", "patch.ku", "trace.ku"):
        source = (ROOT / "dao" / "std" / module_name).read_text(encoding="utf-8")
        ku_ast = ku_parse(source)
        py_ast = py_parse_tokens(py_lex(source))

        assert normalize(ku_ast) == normalize(py_ast)
