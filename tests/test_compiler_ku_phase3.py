"""Phase 3 regression checks for dao/std/compiler.ku."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from dao.compiler import DaoVM  # noqa: E402
from dao.runtime import DaoEnv, Thought  # noqa: E402


@pytest.fixture(autouse=True)
def load_compiler():
    Thought.registry.clear()
    env = DaoEnv()
    env.load(str(ROOT / "dao" / "std" / "compiler.ku"))


def execute(ast: dict):
    bc = Thought.registry["compile_ast"].call([ast])
    return DaoVM().execute(bc), bc


def lit(value):
    return {"type": "literal", "value": value}


def ref(name):
    return {"type": "ref", "value": name}


def op(name, left, right):
    return {"type": "op", "value": name, "children": [left, right]}


def assign(name, value):
    return {"type": "assign", "value": name, "children": [value]}


def block(*children):
    return {"type": "block", "children": list(children)}


def call(name, *args):
    return {"type": "call", "value": name, "children": list(args)}


def ret(value):
    return {"type": "return", "children": [value]}


def thought(name, params, body):
    return {"type": "thought", "value": name, "children": list(params) + [body]}


def list_node(*items):
    return {"type": "list", "children": list(items)}


def dict_node(**items):
    return {
        "type": "dict",
        "children": [
            {"type": "pair", "value": key, "children": [value]}
            for key, value in items.items()
        ],
    }


def index(obj, idx):
    return {"type": "index", "children": [obj, idx]}


def attr(obj, name):
    return {"type": "attr", "value": name, "children": [obj]}


def index_assign(target, value):
    return {"type": "index_assign", "children": [target, value]}


def if_node(cond, yes, no):
    return {"type": "if", "children": [cond, yes, no]}


def while_node(cond, body):
    return {"type": "while", "children": [cond, body]}


def for_node(var, iterable, body):
    return {"type": "for", "value": var, "children": [iterable, body]}


def break_node():
    return {"type": "break"}


def continue_node():
    return {"type": "continue"}


def throw_node(value):
    return {"type": "throw", "children": [value]}


def try_node(body, catch_var="", catch_body=None):
    return {
        "type": "try",
        "value": "",
        "children": [
            body,
            ref(catch_var) if catch_var else lit(""),
            catch_body if catch_body is not None else lit(""),
            lit(""),
        ],
    }


def fib_ast(n):
    minus1 = op("-", ref("n"), lit(1))
    minus2 = op("-", ref("n"), lit(2))
    return block(
        thought(
            "fib",
            ["n"],
            block(
                if_node(
                    op("<=", ref("n"), lit(1)),
                    ret(ref("n")),
                    ret(op("+", call("fib", minus1), call("fib", minus2))),
                )
            ),
        ),
        call("fib", lit(n)),
    )


CASES = [
    ("literal", lit(42), 42),
    ("binary_add", op("+", lit(1), lit(2)), 3),
    ("if_true", if_node(lit(True), lit(1), lit(2)), 1),
    ("if_false", if_node(lit(False), lit(1), lit(2)), 2),
    (
        "while_sum",
        block(
            assign("i", lit(0)),
            assign("s", lit(0)),
            while_node(
                op("<", ref("i"), lit(3)),
                block(
                    assign("s", op("+", ref("s"), ref("i"))),
                    assign("i", op("+", ref("i"), lit(1))),
                ),
            ),
            ref("s"),
        ),
        3,
    ),
    (
        "for_sum",
        block(
            assign("s", lit(0)),
            for_node(
                "x",
                list_node(lit(1), lit(2), lit(3), lit(4)),
                block(assign("s", op("+", ref("s"), ref("x")))),
            ),
            ref("s"),
        ),
        10,
    ),
    (
        "while_break",
        block(
            assign("i", lit(0)),
            while_node(
                lit(True),
                block(
                    assign("i", op("+", ref("i"), lit(1))),
                    if_node(op("==", ref("i"), lit(3)), break_node(), lit(None)),
                ),
            ),
            ref("i"),
        ),
        3,
    ),
    (
        "while_continue",
        block(
            assign("i", lit(0)),
            assign("s", lit(0)),
            while_node(
                op("<", ref("i"), lit(5)),
                block(
                    assign("i", op("+", ref("i"), lit(1))),
                    if_node(op("==", ref("i"), lit(3)), continue_node(), lit(None)),
                    assign("s", op("+", ref("s"), ref("i"))),
                ),
            ),
            ref("s"),
        ),
        12,
    ),
    (
        "for_break",
        block(
            assign("s", lit(0)),
            for_node(
                "x",
                list_node(lit(1), lit(2), lit(3), lit(4)),
                block(
                    if_node(op("==", ref("x"), lit(3)), break_node(), lit(None)),
                    assign("s", op("+", ref("s"), ref("x"))),
                ),
            ),
            ref("s"),
        ),
        3,
    ),
    (
        "for_continue",
        block(
            assign("s", lit(0)),
            for_node(
                "x",
                list_node(lit(1), lit(2), lit(3), lit(4)),
                block(
                    if_node(op("==", ref("x"), lit(3)), continue_node(), lit(None)),
                    assign("s", op("+", ref("s"), ref("x"))),
                ),
            ),
            ref("s"),
        ),
        7,
    ),
    (
        "for_body_index_assign_discards_value",
        block(
            assign("d", dict_node()),
            for_node(
                "x",
                list_node(lit("a"), lit("b")),
                block(index_assign(index(ref("d"), ref("x")), ref("x"))),
            ),
            ref("d"),
        ),
        {"a": "a", "b": "b"},
    ),
    ("list_literal", list_node(lit(1), lit(2), lit(3)), [1, 2, 3]),
    ("dict_literal", dict_node(a=lit(1), b=lit(2)), {"a": 1, "b": 2}),
    (
        "index_lookup",
        block(assign("arr", list_node(lit(10), lit(20))), index(ref("arr"), lit(1))),
        20,
    ),
    (
        "attr_access",
        block(assign("obj", dict_node(name=lit("dao"))), attr(ref("obj"), "name")),
        "dao",
    ),
    (
        "index_assign",
        block(
            assign("arr", list_node(lit(10), lit(20), lit(30))),
            index_assign(index(ref("arr"), lit(1)), lit(99)),
            index(ref("arr"), lit(1)),
        ),
        99,
    ),
    (
        "try_success",
        try_node(lit("ok"), "err", lit("caught")),
        "ok",
    ),
    (
        "try_catch_throw",
        try_node(throw_node(lit("boom")), "err", ref("err")),
        "boom",
    ),
    (
        "try_catch_literal",
        try_node(throw_node(lit("boom")), "err", lit("caught")),
        "caught",
    ),
    (
        "try_catch_runtime_error",
        try_node(index(list_node(), lit(0)), "err", ref("err")),
        "list index out of range",
    ),
    (
        "try_catch_name_error",
        try_node(ref("missing_name"), "err", ref("err")),
        "ku-vm: 'missing_name' 未定义",
    ),
    (
        "try_catch_thought_throw",
        block(
            thought("fail", [], block(throw_node(lit("nested")))),
            try_node(call("fail"), "err", ref("err")),
        ),
        "nested",
    ),
    (
        "thought_call",
        block(
            thought("add", ["a", "b"], block(ret(op("+", ref("a"), ref("b"))))),
            call("add", lit(3), lit(4)),
        ),
        7,
    ),
    ("recursive_thought", fib_ast(10), 55),
]


@pytest.mark.parametrize(("name", "ast", "expected"), CASES, ids=[c[0] for c in CASES])
def test_compiler_ku_baseline(name, ast, expected):
    result, bc = execute(ast)
    assert result == expected, (
        f"{name}: expected {expected!r}, got {result!r}; "
        f"bc={json.dumps(bc, ensure_ascii=False)}"
    )


def test_compiler_ku_uncaught_throw():
    with pytest.raises(RuntimeError, match="boom"):
        execute(throw_node(lit("boom")))
