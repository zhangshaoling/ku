"""Generate the semantic std combo demo bytecode.

This is a bootstrap/build helper. The generated JSON is meant to be executed
directly by dao/dao_core.c without invoking the Python runtime.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from dao.runtime import DaoEnv, Thought  # noqa: E402


OUT = ROOT / "demos" / "semantic_std_combo.kub.json"


def lit(value):
    return {"type": "literal", "value": value}


def ref(name):
    return {"type": "ref", "value": name}


def assign(name, value):
    return {"type": "assign", "value": name, "children": [value]}


def block(*children):
    return {"type": "block", "children": list(children)}


def call_node(name, *args):
    return {"type": "call", "value": name, "children": list(args)}


def list_node(*items):
    return {"type": "list", "children": list(items)}


def pair(key, value):
    return {"type": "pair", "value": key, "children": [value]}


def dict_node(*pairs):
    return {"type": "dict", "children": list(pairs)}


def expr_node(value):
    if isinstance(value, dict):
        return dict_node(*(pair(key, expr_node(child)) for key, child in value.items()))
    if isinstance(value, list):
        return list_node(*(expr_node(item) for item in value))
    return lit(value)


def parse_file(path: Path):
    source = path.read_text(encoding="utf-8")
    tokens = Thought.registry["lex"].call([source])
    return Thought.registry["parse_tokens"].call([tokens])


def load_compiler():
    Thought.registry.clear()
    env = DaoEnv()
    env.load(str(ROOT / "dao" / "std" / "lexer.ku"))
    env.load(str(ROOT / "dao" / "std" / "parser.ku"))
    env.load(str(ROOT / "dao" / "std" / "compiler.ku"))


def compile_ast(ast):
    return Thought.registry["compile_ast"].call([ast])


def build_demo_ast():
    ast = {"type": "block", "children": []}
    for module_name in ("semantic_core.ku", "memory_env.ku", "patch.ku", "trace.ku"):
        ast["children"].extend(parse_file(ROOT / "dao" / "std" / module_name)["children"])
    ast["children"].append(
        block(
            assign(
                "ast",
                call_node(
                    "语义_构造无参思",
                    lit("fix_bug"),
                    expr_node(["observe tests.fail", {"patch": {"scope": "minimal"}}, "verify"]),
                ),
            ),
            assign("env", call_node("环境_新", lit("combo-env"))),
            assign("env", call_node("环境_定义思", ref("env"), lit("fix_bug"), ref("ast"))),
            assign(
                "patch",
                call_node(
                    "补丁_替换",
                    list_node(lit("children"), lit(0)),
                    expr_node({"type": "literal", "value": 2}),
                    expr_node({}),
                    lit("demo"),
                ),
            ),
            assign("effect", call_node("补丁_转影响", ref("patch"))),
            assign("trace", call_node("轨迹_新", lit("combo-trace"))),
            assign("trace", call_node("轨迹_记录成功", ref("trace"), ref("effect"), lit("fix_bug"), ref("ast"))),
            dict_node(pair("env", ref("env")), pair("trace", ref("trace"))),
        )
    )
    return ast


def main():
    load_compiler()
    bytecode = compile_ast(build_demo_ast())
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(bytecode, ensure_ascii=False, separators=(",", ":")) + "\n", encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)}")
    print(f"{len(bytecode['constants'])} constants, {len(bytecode['instructions'])} instructions")


if __name__ == "__main__":
    main()
