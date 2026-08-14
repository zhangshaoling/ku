import json
import os
import subprocess
from pathlib import Path

import pytest

from dao.compiler import DaoVM
from dao.dao_lexer import lex
from dao.dao_parser import parse_tokens
from dao.runtime import DaoEnv, Thought


ROOT = Path(__file__).resolve().parents[1]
C_VM_WSL_PATH = "/mnt/d/tmp/dao_core_parity_pytest"
C_VM_NATIVE_PATH = ROOT / "dao" / "dao_core.exe"


def to_wsl_path(path):
    resolved = path.resolve()
    drive = resolved.drive.rstrip(":").lower()
    tail = resolved.relative_to(resolved.anchor).as_posix()
    return f"/mnt/{drive}/{tail}"


def c_vm_path(cmd, path):
    if cmd and cmd[0] == "wsl":
        return to_wsl_path(path)
    return Path(path).resolve().as_posix()


def lit(value):
    return {"type": "literal", "value": value}


def ref(name):
    return {"type": "ref", "value": name}


def op(name, left, right):
    return {"type": "op", "value": name, "children": [left, right]}


def unary_op(name, value):
    return {"type": "op", "value": name, "children": [value]}


def assign(name, value):
    return {"type": "assign", "value": name, "children": [value]}


def block(*children):
    return {"type": "block", "children": list(children)}


def ret(value):
    return {"type": "return", "children": [value]}


def thought(name, params, body):
    return {"type": "thought", "value": name, "children": list(params) + [body]}


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


def try_node(body, catch_var="", catch_body=None, finally_body=None):
    return {
        "type": "try",
        "value": "",
        "children": [
            body,
            ref(catch_var) if catch_var else lit(""),
            catch_body if catch_body is not None else lit(""),
            finally_body if finally_body is not None else lit(""),
        ],
    }


def list_node(*items):
    return {"type": "list", "children": list(items)}


def pair(key, value):
    return {"type": "pair", "value": key, "children": [value]}


def dict_node(*pairs):
    return {"type": "dict", "children": list(pairs)}


def index_node(obj, idx):
    return {"type": "index", "children": [obj, idx]}


def index_assign_node(target, value):
    return {"type": "index_assign", "children": [target, value]}


def call_node(name, *args):
    return {"type": "call", "value": name, "children": list(args)}


def expr_node(value):
    if isinstance(value, dict):
        return dict_node(*(pair(key, expr_node(child)) for key, child in value.items()))
    if isinstance(value, list):
        return list_node(*(expr_node(item) for item in value))
    return lit(value)


def parse_file(path):
    tokens = lex(path.read_text(encoding="utf-8"))
    tokens.append({"type": "eof", "value": "", "line": 0, "col": 0, "pos": 0})
    return parse_tokens(tokens)


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
                    ret(op("+", call_node("fib", minus1), call_node("fib", minus2))),
                )
            ),
        ),
        call_node("fib", lit(n)),
    )


def nested_thought_ast():
    return block(
        thought(
            "outer",
            ["x"],
            block(
                thought("inner", ["y"], block(ret(op("*", ref("y"), lit(2))))),
                ret(op("+", call_node("inner", ref("x")), lit(1))),
            ),
        ),
        call_node("outer", lit(5)),
    )


def closure_capture_ast():
    return block(
        assign("base", lit(10)),
        thought("add_base", ["x"], block(ret(op("+", ref("base"), ref("x"))))),
        call_node("add_base", lit(5)),
    )


def recursive_copy_ast(value_ast):
    dict_branch = block(
        assign("out", dict_node()),
        for_node(
            "k",
            call_node("keys", ref("v")),
            block(
                index_assign_node(
                    index_node(ref("out"), ref("k")),
                    call_node("copy", index_node(ref("v"), ref("k"))),
                )
            ),
        ),
        ret(ref("out")),
    )
    list_branch = block(
        assign("out", list_node()),
        for_node(
            "x",
            ref("v"),
            block(assign("out", op("+", ref("out"), list_node(call_node("copy", ref("x")))))),
        ),
        ret(ref("out")),
    )
    return block(
        thought(
            "copy",
            ["v"],
            block(
                ret(
                    if_node(
                        op("==", call_node("type", ref("v")), lit("dict")),
                        dict_branch,
                        if_node(
                            op("==", call_node("type", ref("v")), lit("list")),
                            list_branch,
                            ref("v"),
                        ),
                    )
                )
            ),
        ),
        call_node("copy", value_ast),
    )


@pytest.fixture(scope="module")
def c_vm_cmd():
    build_native = subprocess.run(
        [
            "powershell",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(ROOT / "tools" / "build_dao_core.ps1"),
        ],
        cwd=ROOT,
        capture_output=True,
        timeout=120,
    )
    if build_native.returncode == 0 and C_VM_NATIVE_PATH.exists():
        return [str(C_VM_NATIVE_PATH)]

    # build 脚本失败时（如 PATH 无 gcc/cl），复用已存在的 dao_core.exe。
    # 对应 Phase 5 handoff 状态：原生 exe 已存在且能跑 demos。
    if C_VM_NATIVE_PATH.exists():
        return [str(C_VM_NATIVE_PATH)]

    probe = subprocess.run(
        ["wsl", "-d", "kali-linux", "--", "true"],
        capture_output=True,
        timeout=10,
    )
    if probe.returncode != 0:
        pytest.skip("WSL kali-linux is not available from this test environment")

    build = subprocess.run(
        [
            "wsl",
            "-d",
            "kali-linux",
            "--",
            "bash",
            "-lc",
            "cd /mnt/d/Tools/Dao && gcc -o /mnt/d/tmp/dao_core_parity_pytest dao/dao_core.c vendor/sqlite3.c -lm -Wall -O2",
        ],
        capture_output=True,
        timeout=120,
    )
    if build.returncode != 0:
        pytest.skip("dao_core.c could not be compiled with WSL gcc")
    return ["wsl", "-d", "kali-linux", "--", C_VM_WSL_PATH]


@pytest.fixture(scope="module", autouse=True)
def load_compiler():
    Thought.registry.clear()
    env = DaoEnv()
    env.load(str(ROOT / "dao" / "std" / "compiler.ku"))


def compile_ast(ast):
    return Thought.registry["compile_ast"].call([ast])


def run_c_vm(cmd, bytecode):
    result = subprocess.run(
        cmd,
        input=json.dumps(bytecode, ensure_ascii=False).encode("utf-8"),
        capture_output=True,
        timeout=10,
    )
    stderr = result.stderr.decode("utf-8", "replace")
    stdout = result.stdout.decode("utf-8", "replace").strip()
    assert result.returncode == 0, stderr
    return stdout


def run_c_vm_with_env(cmd, bytecode, extra_env):
    env = os.environ.copy()
    env.update(extra_env)
    result = subprocess.run(
        cmd,
        input=json.dumps(bytecode, ensure_ascii=False).encode("utf-8"),
        capture_output=True,
        timeout=10,
        env=env,
    )
    stderr = result.stderr.decode("utf-8", "replace")
    stdout = result.stdout.decode("utf-8", "replace").strip()
    assert result.returncode == 0, stderr
    return stdout


def run_c_vm_process(cmd, bytecode):
    return subprocess.run(
        cmd,
        input=json.dumps(bytecode, ensure_ascii=False).encode("utf-8"),
        capture_output=True,
        timeout=10,
    )


def run_c_vm_process_ascii_json(cmd, bytecode):
    return subprocess.run(
        cmd,
        input=json.dumps(bytecode, ensure_ascii=True).encode("utf-8"),
        capture_output=True,
        timeout=10,
    )


def format_python_value(value):
    if value is True:
        return "true"
    if value is False:
        return "false"
    if value is None:
        return "null"
    if isinstance(value, str):
        return json.dumps(value, ensure_ascii=False)
    if isinstance(value, list):
        return "[" + ", ".join(format_python_value(item) for item in value) + "]"
    if isinstance(value, dict):
        items = [
            f"{json.dumps(key, ensure_ascii=False)}: {format_python_value(item)}"
            for key, item in value.items()
        ]
        return "{" + ", ".join(items) + "}"
    return str(value)


CASES = [
    ("literal_number", lit(42)),
    ("binary_add", op("+", lit(1), lit(2))),
    ("binary_sub", op("-", lit(5), lit(3))),
    ("equality_number", op("==", lit(4), lit(4))),
    ("comparison_lt", op("<", lit(1), lit(2))),
    ("if_true", if_node(lit(True), lit(7), lit(9))),
    ("if_false", if_node(lit(False), lit(7), lit(9))),
    ("list_literal", list_node(lit(1), lit(2), lit(3))),
    ("dict_literal", dict_node(pair("a", lit(1)), pair("b", lit(2)))),
    ("index_lookup_list", index_node(list_node(lit(10), lit(20)), lit(1))),
    ("index_lookup_dict", index_node(dict_node(pair("a", lit(11)), pair("b", lit(22))), lit("b"))),
    ("string_literal", lit("abc")),
    ("string_concat", op("+", lit("a"), lit("b"))),
    ("string_concat_number", op("+", lit("n="), lit(3))),
    ("list_concat", op("+", list_node(lit(1)), list_node(lit(2)))),
    ("builtin_str_split", call_node("str_split", lit("observe tests.fail"), lit(" "))),
    ("builtin_str_contains_true", call_node("str_contains", lit("dao vm"), lit("vm"))),
    ("builtin_str_is_empty_spaces", call_node("str_is_empty", lit("  \t"))),
    ("builtin_str_starts_with_true", call_node("str_starts_with", lit("dao vm"), lit("dao"))),
    ("builtin_str_ends_with_true", call_node("str_ends_with", lit("dao vm"), lit("vm"))),
    ("builtin_str_upper_ascii", call_node("str_upper", lit("Dao天书"))),
    ("builtin_str_lower_ascii", call_node("str_lower", lit("Dao天书"))),
    ("builtin_int_string", call_node("int", lit("42"))),
    ("builtin_float_string", call_node("float", lit("1.5"))),
    (
        "builtin_run_bytecode",
        call_node(
            "run_bytecode",
            expr_node(
                {
                    "format": "kub",
                    "version": "0.1",
                    "constants": [1, 2],
                    "instructions": [["LOAD_CONST", 0], ["LOAD_CONST", 1], ["BINARY_OP", "+"], ["RETURN"]],
                    "entry": 0,
                }
            ),
        ),
    ),
    ("equality_string_true", op("==", lit("x"), lit("x"))),
    ("equality_string_false", op("==", lit("x"), lit("y"))),
    ("builtin_len_list", call_node("len", list_node(lit(1), lit(2), lit(3)))),
    ("builtin_now_fmt_len", call_node("len", call_node("now_fmt"))),
    ("builtin_type_string", call_node("type", lit("abc"))),
    ("builtin_has_true", call_node("has", dict_node(pair("a", lit(1))), lit("a"))),
    ("builtin_is_none_true", call_node("is_none", lit(None))),
    ("builtin_items_dict", call_node("items", dict_node(pair("a", lit(1)), pair("b", lit(2))))),
    ("builtin_slice_list", call_node("slice", list_node(lit(1), lit(2), lit(3), lit(4)), lit(1), lit(3))),
    (
        "nested_list_dict",
        list_node(
            dict_node(pair("x", lit(1))),
            dict_node(pair("y", list_node(lit(2), lit(3)))),
        ),
    ),
    (
        "block_assign_ref",
        block(assign("x", lit(4)), assign("y", op("+", ref("x"), lit(3))), ref("y")),
    ),
    (
        "index_assign_list",
        block(
            assign("xs", list_node(lit(1), lit(2), lit(3))),
            index_assign_node(index_node(ref("xs"), lit(1)), lit(9)),
            index_node(ref("xs"), lit(1)),
        ),
    ),
    (
        "thought_call",
        block(
            thought("add", ["a", "b"], block(ret(op("+", ref("a"), ref("b"))))),
            call_node("add", lit(3), lit(4)),
        ),
    ),
    (
        "while_loop",
        block(
            assign("i", lit(0)),
            assign("s", lit(0)),
            while_node(
                op("<", ref("i"), lit(4)),
                block(
                    assign("s", op("+", ref("s"), ref("i"))),
                    assign("i", op("+", ref("i"), lit(1))),
                ),
            ),
            ref("s"),
        ),
    ),
    (
        "for_loop",
        block(
            assign("s", lit(0)),
            for_node(
                "x",
                list_node(lit(1), lit(2), lit(3), lit(4)),
                block(assign("s", op("+", ref("s"), ref("x")))),
            ),
            ref("s"),
        ),
    ),
    (
        "for_body_index_assign_discards_value",
        block(
            assign("d", dict_node()),
            for_node(
                "x",
                list_node(lit("a"), lit("b")),
                block(index_assign_node(index_node(ref("d"), ref("x")), ref("x"))),
            ),
            ref("d"),
        ),
    ),
    (
        "for_keys_dict_copy",
        block(
            assign("d", dict_node(pair("a", lit(1)), pair("b", lit(2)))),
            assign("out", dict_node()),
            for_node(
                "k",
                call_node("keys", ref("d")),
                block(index_assign_node(index_node(ref("out"), ref("k")), index_node(ref("d"), ref("k")))),
            ),
            ref("out"),
        ),
    ),
    ("recursive_copy_list", recursive_copy_ast(list_node(lit("x"), lit("y")))),
    (
        "recursive_copy_nested_dict",
        recursive_copy_ast(
            dict_node(
                pair("type", lit("literal")),
                pair("value", lit("ok")),
                pair(
                    "children",
                    list_node(dict_node(pair("type", lit("literal")), pair("value", lit("child")))),
                ),
            )
        ),
    ),
    ("if_without_else_false", {"type": "if", "children": [lit(False), lit(1)]}),
    ("and_short_circuit_false", op("and", lit(False), ref("missing_right"))),
    ("or_short_circuit_true", op("or", lit(True), ref("missing_right"))),
    ("builtin_ord_chr", call_node("chr", call_node("ord", lit("A")))),
    ("try_success", try_node(lit("ok"), "err", lit("caught"))),
    ("try_catch_throw", try_node(throw_node(lit("boom")), "err", ref("err"))),
    ("try_catch_index_error", try_node(index_node(list_node(), lit(0)), "err", ref("err"))),
    (
        "try_finally_shape",
        block(
            assign("events", list_node()),
            try_node(
                block(
                    assign("events", op("+", ref("events"), list_node(lit("try")))),
                    lit("body"),
                ),
                "",
                None,
                block(assign("events", op("+", ref("events"), list_node(lit("finally"))))),
            ),
            ref("events"),
        ),
    ),
    ("try_catch_call_unknown_builtin", try_node(call_node("missing_builtin"), "err", ref("err"))),
    ("try_catch_string_index_out_of_range", try_node(index_node(lit("abc"), lit(9)), "err", ref("err"))),
    (
        "try_catch_set_index_error",
        try_node(index_assign_node(index_node(lit("abc"), lit(0)), lit("x")), "err", ref("err")),
    ),
    ("try_catch_unknown_binary_op", try_node(op("??", lit(1), lit(2)), "err", ref("err"))),
    ("unary_not", unary_op("not", lit(False))),
    ("unary_minus", unary_op("-", lit(7))),
    ("comparison_gt", op(">", lit(4), lit(2))),
    ("comparison_ge", op(">=", lit(4), lit(4))),
    ("comparison_le", op("<=", lit(3), lit(4))),
    ("comparison_ne", op("!=", lit(3), lit(4))),
    ("nested_thought_call", nested_thought_ast()),
    ("try_catch_name_error", try_node(ref("missing_name"), "err", ref("err"))),
    (
        "try_catch_thought_throw",
        block(
            thought("fail", [], block(throw_node(lit("nested")))),
            try_node(call_node("fail"), "err", ref("err")),
        ),
    ),
    (
        "nested_try_catch",
        try_node(
            try_node(throw_node(lit("inner")), "inner_err", throw_node(ref("inner_err"))),
            "outer_err",
            ref("outer_err"),
        ),
    ),
    ("dict_missing_key_returns_null", index_node(dict_node(pair("a", lit(1))), lit("missing"))),
    ("string_index", index_node(lit("abc"), lit(1))),
    ("utf8_string_len", call_node("len", lit("天书"))),
    ("utf8_string_index", index_node(lit("天书"), lit(1))),
    ("utf8_string_slice", call_node("slice", lit("天书道"), lit(0), lit(2))),
    ("utf8_ord_chr", call_node("chr", call_node("ord", lit("书")))),
    ("negative_list_index", index_node(list_node(lit(10), lit(20), lit(30)), lit(-1))),
    ("list_equality_true", op("==", list_node(lit(1), lit(2)), list_node(lit(1), lit(2)))),
    (
        "dict_equality_true",
        op("==", dict_node(pair("a", lit(1))), dict_node(pair("a", lit(1)))),
    ),
    ("closure_capture", closure_capture_ast()),
    ("builtin_is_str_true", call_node("is_str", lit("abc"))),
    ("builtin_is_list_true", call_node("is_list", list_node(lit(1)))),
    ("builtin_is_dict_true", call_node("is_dict", dict_node(pair("a", lit(1))))),
    ("builtin_keys_dict", call_node("keys", dict_node(pair("a", lit(1)), pair("b", lit(2))))),
    ("numeric_mul", op("*", lit(6), lit(7))),
    ("numeric_div", op("/", lit(7), lit(2))),
    ("numeric_mod", op("%", lit(8), lit(3))),
    ("builtin_slice_string", call_node("slice", lit("abcd"), lit(1), lit(3))),
    ("str_list", call_node("str", list_node(lit(1), lit(2)))),
    ("string_concat_list", op("+", lit("xs="), list_node(lit(1), lit(2)))),
    (
        "nested_container_equality",
        op(
            "==",
            list_node(dict_node(pair("a", list_node(lit(1), lit(2))))),
            list_node(dict_node(pair("a", list_node(lit(1), lit(2))))),
        ),
    ),
    (
        "set_index_negative_list",
        block(
            assign("xs", list_node(lit(1), lit(2), lit(3))),
            index_assign_node(index_node(ref("xs"), lit(-1)), lit(9)),
            index_node(ref("xs"), lit(-1)),
        ),
    ),
    (
        "set_index_dict_new_key",
        block(
            assign("d", dict_node(pair("a", lit(1)))),
            index_assign_node(index_node(ref("d"), lit("b")), lit(6)),
            index_node(ref("d"), lit("b")),
        ),
    ),
    (
        "dict_update_existing_key",
        block(
            assign("d", dict_node(pair("a", lit(1)))),
            index_assign_node(index_node(ref("d"), lit("a")), lit(5)),
            index_node(ref("d"), lit("a")),
        ),
    ),
    (
        "builtin_push_mutation",
        block(
            assign("xs", list_node(lit(1))),
            call_node("push", ref("xs"), lit(2)),
            call_node("len", ref("xs")),
        ),
    ),
    (
        "while_break",
        block(
            assign("i", lit(0)),
            while_node(
                lit(True),
                block(
                    if_node(op("==", ref("i"), lit(3)), break_node(), lit(None)),
                    assign("i", op("+", ref("i"), lit(1))),
                ),
            ),
            ref("i"),
        ),
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
    ),
    ("recursive_thought", fib_ast(6)),
]


@pytest.mark.parametrize(("name", "ast"), CASES)
def test_c_vm_matches_python_daovm_for_p0_bytecode(c_vm_cmd, name, ast):
    bytecode = compile_ast(ast)
    python_result = DaoVM().execute(bytecode)
    c_result = run_c_vm(c_vm_cmd, bytecode)

    assert c_result == format_python_value(python_result)


ERROR_CASES = [
    ("missing_name_error", ref("missing_name"), "NameError"),
    ("throw_uncaught", throw_node(lit("boom")), "RuntimeError: boom"),
    ("list_index_out_of_range_error", index_node(list_node(), lit(0)), "list index out of range"),
    ("string_index_out_of_range_error", index_node(lit("abc"), lit(9)), "string index out of range"),
    (
        "set_index_string_error",
        block(
            assign("s", lit("abc")),
            index_assign_node(index_node(ref("s"), lit(1)), lit("x")),
        ),
        "object does not support item assignment",
    ),
    ("call_unknown_builtin_error", call_node("missing_builtin"), "NameError"),
]


@pytest.mark.parametrize(("name", "ast", "stderr_text"), ERROR_CASES)
def test_c_vm_reports_expected_uncaught_errors(c_vm_cmd, name, ast, stderr_text):
    bytecode = compile_ast(ast)
    result = run_c_vm_process(c_vm_cmd, bytecode)
    stderr = result.stderr.decode("utf-8", "replace")

    assert result.returncode != 0
    assert stderr_text in stderr


def test_c_vm_catches_non_callable_bytecode_error(c_vm_cmd):
    bytecode = {
        "format": "kub",
        "version": "0.1",
        "constants": [1],
        "instructions": [
            ["TRY_BEGIN", 5],
            ["LOAD_CONST", 0],
            ["CALL", 0],
            ["TRY_END"],
            ["JUMP", 3],
            ["STORE_NAME", "err"],
            ["LOAD_NAME", "err"],
            ["RETURN"],
        ],
        "entry": 0,
    }

    assert run_c_vm(c_vm_cmd, bytecode) == '"value is not callable"'


def test_c_vm_decodes_json_unicode_escapes_and_prints_escaped_strings(c_vm_cmd):
    value = '天\n"书"\\道'
    bytecode = {
        "format": "kub",
        "version": "0.1",
        "constants": [value],
        "instructions": [["LOAD_CONST", 0], ["RETURN"]],
        "entry": 0,
    }

    result = run_c_vm_process_ascii_json(c_vm_cmd, bytecode)
    stdout = result.stdout.decode("utf-8", "replace").strip()
    stderr = result.stderr.decode("utf-8", "replace")

    assert result.returncode == 0, stderr
    assert stdout == json.dumps(value, ensure_ascii=False)


def test_c_vm_file_directory_time_and_system_builtins(c_vm_cmd, tmp_path):
    dir_path = c_vm_path(c_vm_cmd, tmp_path)
    file_path = c_vm_path(c_vm_cmd, tmp_path / "dao_builtin_probe.txt")
    bytecode = compile_ast(
        block(
            assign("wrote", call_node("write_file", lit(file_path), lit("天书"))),
            assign("exists_before", call_node("path_exists", lit(file_path))),
            assign("content", call_node("read_file", lit(file_path))),
            assign("files", call_node("list_dir", lit(dir_path))),
            assign("sys", call_node("system", lit("echo dao"))),
            assign("deleted", call_node("delete_file", lit(file_path))),
            assign("exists_after", call_node("path_exists", lit(file_path))),
            dict_node(
                pair("wrote", ref("wrote")),
                pair("exists_before", ref("exists_before")),
                pair("content", ref("content")),
                pair("listed", call_node("str_contains", call_node("str", ref("files")), lit("dao_builtin_probe.txt"))),
                pair("system_stdout", index_node(ref("sys"), lit("stdout"))),
                pair("now_positive", op(">", call_node("now"), lit(0))),
                pair("deleted", ref("deleted")),
                pair("exists_after", ref("exists_after")),
            ),
        )
    )

    # system is disabled by default; verify opt-in path separately.
    default_result = run_c_vm(c_vm_cmd, bytecode)
    optin_result = run_c_vm_with_env(c_vm_cmd, bytecode, {"DAO_ALLOW_SYSTEM": "1"})

    assert '"wrote": true' in default_result
    assert '"exists_before": true' in default_result
    assert '"content": "天书"' in default_result
    assert '"listed": true' in default_result
    # Default: system builtin is disabled; stderr explains why.
    assert '"system_stdout": ""' in default_result
    assert '"now_positive": true' in default_result
    assert '"deleted": true' in default_result
    assert '"exists_after": false' in default_result

    # Opt-in: system builtin runs and returns expected output.
    assert '"system_stdout": "dao' in optin_result
    assert '"now_positive": true' in optin_result


def test_c_vm_m3_now_fmt_sqlite_dao_data(c_vm_cmd, tmp_path):
    db_path = c_vm_path(c_vm_cmd, tmp_path / "m3_test.db")
    data_dir = c_vm_path(c_vm_cmd, tmp_path / "m3_data")
    bytecode = compile_ast(
        block(
            assign("ts", call_node("now_fmt")),
            assign("ts_ok", op("==", call_node("len", ref("ts")), lit(19))),
            assign("db", call_node("sqlite_open", lit(db_path))),
            call_node("sqlite_exec", ref("db"), lit("CREATE TABLE IF NOT EXISTS m3 (x TEXT)"), list_node()),
            call_node("sqlite_exec", ref("db"), lit("INSERT INTO m3 VALUES (?)"), list_node(lit("dao"))),
            assign("rows", call_node("sqlite_query", ref("db"), lit("SELECT x FROM m3"), list_node())),
            assign("row_count", call_node("len", ref("rows"))),
            assign("row_x", index_node(index_node(ref("rows"), lit(0)), lit("x"))),
            call_node("sqlite_close", ref("db")),
            assign("dp", call_node("dao_data_path", lit("nested/probe.db"))),
            assign("dp_ok", call_node("str_contains", ref("dp"), lit("nested"))),
            dict_node(
                pair("ts_ok", ref("ts_ok")),
                pair("row_count", ref("row_count")),
                pair("row_x", ref("row_x")),
                pair("dp_ok", ref("dp_ok")),
            ),
        )
    )

    env = os.environ.copy()
    env["DAO_DATA_DIR"] = data_dir
    result = subprocess.run(
        c_vm_cmd,
        input=json.dumps(bytecode, ensure_ascii=False).encode("utf-8"),
        capture_output=True,
        timeout=15,
        env=env,
    )
    stderr = result.stderr.decode("utf-8", "replace")
    stdout = result.stdout.decode("utf-8", "replace").strip()
    assert result.returncode == 0, stderr
    parsed = json.loads(stdout)
    assert parsed == {"ts_ok": True, "row_count": 1, "row_x": "dao", "dp_ok": True}


MEMORY_ENV_C_VM_CASES = [
    ("memory_env_new", call_node("环境_新", lit("env-c"))),
    (
        "memory_env_define_thought",
        block(
            assign("env", call_node("环境_新", lit("env-c"))),
            call_node(
                "环境_定义思",
                ref("env"),
                lit("check"),
                expr_node({"type": "literal", "value": "ok", "children": []}),
            ),
        ),
    ),
    (
        "memory_env_remember",
        block(
            assign("env", call_node("环境_新", lit("env-c"))),
            call_node(
                "环境_记住",
                ref("env"),
                lit("goal"),
                expr_node({"name": "stabilize"}),
                lit("session"),
                expr_node({}),
            ),
        ),
    ),
    (
        "memory_env_register_tool",
        block(
            assign("env", call_node("环境_新", lit("env-c"))),
            call_node("环境_注册工具", ref("env"), lit("verify"), lit("check state"), lit("safe")),
        ),
    ),
    (
        "memory_env_recall_existing",
        block(
            assign("env", call_node("环境_新", lit("env-r"))),
            assign(
                "env",
                call_node(
                    "环境_记住",
                    ref("env"),
                    lit("goal"),
                    expr_node({"name": "stabilize"}),
                    lit("session"),
                    expr_node({}),
                ),
            ),
            call_node("环境_忆起并记录", ref("env"), lit("goal"), expr_node({})),
        ),
    ),
    (
        "memory_env_recall_missing",
        block(
            assign("env", call_node("环境_新", lit("env-m"))),
            call_node("环境_忆起并记录", ref("env"), lit("missing"), lit("default")),
        ),
    ),
    (
        "memory_env_get_trace",
        block(
            assign("env", call_node("环境_新", lit("env-t"))),
            call_node("环境_取轨迹", ref("env")),
        ),
    ),
    (
        "memory_env_get_thought",
        block(
            assign("env", call_node("环境_新", lit("env-s"))),
            assign(
                "env",
                call_node(
                    "环境_定义思",
                    ref("env"),
                    lit("check"),
                    expr_node({"type": "literal", "value": "ok", "children": []}),
                ),
            ),
            call_node("环境_取思", ref("env"), lit("check")),
        ),
    ),
    (
        "memory_env_get_memory",
        block(
            assign("env", call_node("环境_新", lit("env-g"))),
            assign(
                "env",
                call_node(
                    "环境_记住",
                    ref("env"),
                    lit("goal"),
                    expr_node({"name": "stabilize"}),
                    lit("session"),
                    expr_node({}),
                ),
            ),
            call_node("环境_取记忆", ref("env"), lit("goal")),
        ),
    ),
]


@pytest.mark.parametrize(("name", "call_ast"), MEMORY_ENV_C_VM_CASES)
def test_c_vm_executes_memory_env_module_calls(c_vm_cmd, name, call_ast):
    assert_c_vm_module_call_matches_python(c_vm_cmd, "memory_env.ku", call_ast)


TRACE_C_VM_CASES = [
    ("trace_call_effect", call_node("轨迹_调用影响", lit("check_state"), list_node(lit("system.ready")))),
    (
        "trace_record_success",
        block(
            assign("tr", call_node("轨迹_新", lit("trace-1"))),
            assign("eff", call_node("轨迹_结果影响", lit("check_state"))),
            call_node("轨迹_记录成功", ref("tr"), ref("eff"), lit("check_state"), lit("ok")),
        ),
    ),
    ("trace_error_effect", call_node("轨迹_错误影响", lit("missing"), lit("not found"))),
]


@pytest.mark.parametrize(("name", "call_ast"), TRACE_C_VM_CASES)
def test_c_vm_executes_trace_module_calls(c_vm_cmd, name, call_ast):
    assert_c_vm_module_call_matches_python(c_vm_cmd, "trace.ku", call_ast)


SEMANTIC_CORE_C_VM_CASES = [
    ("semantic_node", call_node("语义_节点", lit("literal"), lit("x"), list_node())),
    ("semantic_value_to_node_dict", call_node("语义_值转节点", expr_node({"scope": "minimal", "count": 2}))),
    (
        "semantic_construct_short_thought",
        call_node(
            "语义_构造无参思",
            lit("fix_bug"),
            expr_node(
                [
                    "observe tests.fail",
                    {"locate": {"mode": "causal"}},
                    {"patch": {"scope": "minimal"}},
                    "verify",
                ]
            ),
        ),
    ),
]


@pytest.mark.parametrize(("name", "call_ast"), SEMANTIC_CORE_C_VM_CASES)
def test_c_vm_executes_semantic_core_module_calls(c_vm_cmd, name, call_ast):
    assert_c_vm_module_call_matches_python(c_vm_cmd, "semantic_core.ku", call_ast)


PATCH_C_VM_CASES = [
    (
        "patch_replace",
        call_node(
            "补丁_替换",
            list_node(lit("children"), lit(0)),
            expr_node({"type": "literal", "value": 2}),
            expr_node({}),
            lit("demo"),
        ),
    ),
    (
        "patch_to_effect",
        call_node(
            "补丁_转影响",
            call_node(
                "补丁_替换",
                list_node(lit("children"), lit(0)),
                expr_node({"type": "literal", "value": 2}),
                expr_node({}),
                lit("demo"),
            ),
        ),
    ),
    (
        "patch_apply_replace",
        block(
            assign("p", call_node("补丁_替换", list_node(lit("value")), lit(2), lit(1), lit("demo"))),
            call_node("补丁_应用", expr_node({"type": "literal", "value": 1, "children": []}), ref("p")),
        ),
    ),
    (
        "patch_inverse",
        call_node("补丁_反向", call_node("补丁_替换", list_node(lit("value")), lit(2), lit(1), lit("demo"))),
    ),
    (
        "patch_apply_add",
        block(
            assign(
                "p",
                call_node(
                    "补丁_添加",
                    list_node(lit("children"), lit(1)),
                    expr_node({"type": "literal", "value": 2}),
                    lit("append"),
                ),
            ),
            call_node("补丁_应用", expr_node({"type": "block", "children": [{"type": "literal", "value": 1}]}), ref("p")),
        ),
    ),
    (
        "patch_apply_remove",
        block(
            assign(
                "p",
                call_node(
                    "补丁_删除",
                    list_node(lit("children"), lit(0)),
                    expr_node({"type": "literal", "value": 1}),
                    lit("drop"),
                ),
            ),
            call_node(
                "补丁_应用",
                expr_node({"type": "block", "children": [{"type": "literal", "value": 1}, {"type": "literal", "value": 2}]}),
                ref("p"),
            ),
        ),
    ),
    (
        "patch_apply_nested_replace",
        block(
            assign("p", call_node("补丁_替换", list_node(lit("children"), lit(0), lit("value")), lit(9), lit(1), lit("nested"))),
            call_node("补丁_应用", expr_node({"type": "block", "children": [{"type": "literal", "value": 1}]}), ref("p")),
        ),
    ),
]


@pytest.mark.parametrize(("name", "call_ast"), PATCH_C_VM_CASES)
def test_c_vm_executes_patch_module_calls(c_vm_cmd, name, call_ast):
    assert_c_vm_module_call_matches_python(c_vm_cmd, "patch.ku", call_ast)


COMPILER_C_VM_CASES = [
    (
        "compiler_compile_binary_add",
        call_node(
            "compile_ast",
            expr_node(
                {
                    "type": "op",
                    "value": "+",
                    "children": [
                        {"type": "literal", "value": 1},
                        {"type": "literal", "value": 2},
                    ],
                }
            ),
        ),
        3,
    ),
]


@pytest.mark.parametrize(("name", "call_ast", "expected_execution"), COMPILER_C_VM_CASES)
def test_c_vm_executes_compiler_module_calls(c_vm_cmd, name, call_ast, expected_execution):
    ast = parse_file(ROOT / "dao" / "std" / "compiler.ku")
    ast["children"].append(call_ast)
    bytecode = compile_ast(ast)
    saved_registry = dict(Thought.registry)
    try:
        python_result = DaoVM().execute(bytecode)
        c_result = run_c_vm(c_vm_cmd, bytecode)
    finally:
        Thought.registry.clear()
        Thought.registry.update(saved_registry)

    assert DaoVM().execute(python_result) == expected_execution
    assert c_result == format_python_value(python_result)


def assert_c_vm_module_call_matches_python(c_vm_cmd, module_name, call_ast):
    ast = parse_file(ROOT / "dao" / "std" / module_name)
    ast["children"].append(call_ast)
    bytecode = compile_ast(ast)
    python_result = DaoVM().execute(bytecode)
    c_result = run_c_vm(c_vm_cmd, bytecode)

    assert c_result == format_python_value(python_result)


def test_c_vm_executes_semantic_std_combo_demo(c_vm_cmd):
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
    bytecode = compile_ast(ast)
    python_result = DaoVM().execute(bytecode)
    c_result = run_c_vm(c_vm_cmd, bytecode)

    assert c_result == format_python_value(python_result)


def test_c_vm_executes_committed_semantic_std_combo_demo_file(c_vm_cmd):
    demo_path = ROOT / "demos" / "semantic_std_combo.kub.json"
    result = subprocess.run(
        c_vm_cmd + [c_vm_path(c_vm_cmd, demo_path)],
        capture_output=True,
        timeout=10,
    )
    stdout = result.stdout.decode("utf-8", "replace")
    stderr = result.stderr.decode("utf-8", "replace")

    assert result.returncode == 0, stderr
    assert "combo-env" in stdout
    assert "combo-trace" in stdout
    assert "fix_bug" in stdout
    assert "patch" in stdout


def test_c_vm_executes_committed_frontend_compile_demo_file(c_vm_cmd):
    demo_path = ROOT / "demos" / "frontend_compile_demo.kub.json"
    bytecode = json.loads(demo_path.read_text(encoding="utf-8"))
    saved_registry = dict(Thought.registry)
    try:
        python_result = DaoVM().execute(bytecode)
    finally:
        Thought.registry.clear()
        Thought.registry.update(saved_registry)

    result = subprocess.run(
        c_vm_cmd + [c_vm_path(c_vm_cmd, demo_path)],
        capture_output=True,
        timeout=60,
    )
    stdout = result.stdout.decode("utf-8", "replace").strip()
    stderr = result.stderr.decode("utf-8", "replace")

    assert result.returncode == 0, stderr
    assert python_result == {"type": "literal", "value": "x", "children": []}
    assert stdout == format_python_value(python_result)


def test_c_vm_bootstrap_cli_compiles_and_runs_ku_source(c_vm_cmd, tmp_path):
    bootstrap_path = ROOT / "demos" / "frontend_bootstrap.kub.json"
    # Keep the path ASCII: Windows native argv can still pass non-ASCII file
    # names to the C VM as "??" on CI, while the source contents remain UTF-8.
    source_path = tmp_path / "increment.ku"
    source_path.write_text(
        "思 加一(数) {\n"
        "  数 + 1\n"
        "}\n"
        "加一(41)\n",
        encoding="utf-8",
    )

    result = subprocess.run(
        c_vm_cmd + ["--bootstrap", c_vm_path(c_vm_cmd, bootstrap_path), c_vm_path(c_vm_cmd, source_path)],
        capture_output=True,
        timeout=60,
    )
    stdout = result.stdout.decode("utf-8", "replace").strip()
    stderr = result.stderr.decode("utf-8", "replace")

    assert result.returncode == 0, stderr
    assert stdout == "42"




def test_c_vm_bootstrap_cli_runs_golden_path_demo(c_vm_cmd):
    bootstrap_path = ROOT / "demos" / "frontend_bootstrap.kub.json"
    source_path = ROOT / "demos" / "golden_path.ku"

    result = subprocess.run(
        c_vm_cmd + ["--bootstrap", c_vm_path(c_vm_cmd, bootstrap_path), c_vm_path(c_vm_cmd, source_path)],
        capture_output=True,
        timeout=60,
    )
    stdout = result.stdout.decode("utf-8", "replace").strip()
    stderr = result.stderr.decode("utf-8", "replace")

    assert result.returncode == 0, stderr
    assert '"thought": "加一"' in stdout
    assert '"code": "thought"' in stdout
    assert '"memory": "thought = code = memory"' in stdout
    assert '"result": 42' in stdout


def test_c_vm_bootstrap_cli_loads_semantic_core_source_and_runs_user_program(c_vm_cmd, tmp_path):
    bootstrap_path = ROOT / "demos" / "frontend_bootstrap.kub.json"
    semantic_core_path = ROOT / "dao" / "std" / "semantic_core.ku"
    source_path = tmp_path / "use_semantic_core.ku"
    source_path.write_text(
        '语义_构造无参思("fix_bug", ["observe tests.fail", {"patch": {"scope": "minimal"}}, "verify"])\n',
        encoding="utf-8",
    )

    result = subprocess.run(
        c_vm_cmd
        + [
            "--bootstrap",
            c_vm_path(c_vm_cmd, bootstrap_path),
            c_vm_path(c_vm_cmd, semantic_core_path),
            c_vm_path(c_vm_cmd, source_path),
        ],
        capture_output=True,
        timeout=60,
    )
    stdout = result.stdout.decode("utf-8", "replace").strip()
    stderr = result.stderr.decode("utf-8", "replace")

    assert result.returncode == 0, stderr
    assert '"type": "thought"' in stdout
    assert '"value": "fix_bug"' in stdout
    assert '"value": "observe"' in stdout
    assert '"value": "verify"' in stdout


def test_c_vm_bootstrap_cli_loads_math_source_and_runs_user_program(c_vm_cmd, tmp_path):
    bootstrap_path = ROOT / "demos" / "frontend_bootstrap.kub.json"
    math_path = ROOT / "dao" / "std" / "math.ku"
    source_path = tmp_path / "use_math.ku"
    source_path.write_text('{"sum": 求和([1, 2, 3]), "fib": 斐波那契(10)}\n', encoding="utf-8")

    result = subprocess.run(
        c_vm_cmd + ["--bootstrap", c_vm_path(c_vm_cmd, bootstrap_path), c_vm_path(c_vm_cmd, math_path), c_vm_path(c_vm_cmd, source_path)],
        capture_output=True,
        timeout=60,
    )
    stdout = result.stdout.decode("utf-8", "replace").strip()
    stderr = result.stderr.decode("utf-8", "replace")

    assert result.returncode == 0, stderr
    assert '"sum": 6' in stdout
    assert '"fib": 55' in stdout


def test_c_vm_bootstrap_cli_loads_string_source_and_runs_utf8_user_program(c_vm_cmd, tmp_path):
    bootstrap_path = ROOT / "demos" / "frontend_bootstrap.kub.json"
    string_path = ROOT / "dao" / "std" / "string.ku"
    source_path = tmp_path / "use_string.ku"
    source_path.write_text(
        '{"joined": 连接(["天", "书"], ""), "reverse": 反转文本("道书天"), "char": 取字符("天书", 1)}\n',
        encoding="utf-8",
    )

    result = subprocess.run(
        c_vm_cmd + ["--bootstrap", c_vm_path(c_vm_cmd, bootstrap_path), c_vm_path(c_vm_cmd, string_path), c_vm_path(c_vm_cmd, source_path)],
        capture_output=True,
        timeout=60,
    )
    stdout = result.stdout.decode("utf-8", "replace").strip()
    stderr = result.stderr.decode("utf-8", "replace")

    assert result.returncode == 0, stderr
    assert '"joined": "天书"' in stdout
    assert '"reverse": "天书道"' in stdout
    assert '"char": "书"' in stdout


def test_c_vm_bootstrap_cli_runs_core_self_check(c_vm_cmd, tmp_path):
    bootstrap_path = ROOT / "demos" / "frontend_bootstrap.kub.json"
    test_core_path = ROOT / "dao" / "test_core.ku"
    source_path = tmp_path / "run_core.ku"
    source_path.write_text("运行核心自检()\n", encoding="utf-8")

    result = subprocess.run(
        c_vm_cmd + ["--bootstrap", c_vm_path(c_vm_cmd, bootstrap_path), c_vm_path(c_vm_cmd, test_core_path), c_vm_path(c_vm_cmd, source_path)],
        capture_output=True,
        timeout=90,
    )
    stdout = result.stdout.decode("utf-8", "replace")
    stderr = result.stderr.decode("utf-8", "replace")

    assert result.returncode == 0, stderr
    assert '"网络测试通过: GET OK"' in stdout
    assert '"核心自检通过"' in stdout
    assert stdout.strip().endswith("true")


def test_c_vm_bootstrap_cli_supports_parse_and_list_thoughts(c_vm_cmd, tmp_path):
    bootstrap_path = ROOT / "demos" / "frontend_bootstrap.kub.json"
    source_path = tmp_path / "reflect.ku"
    source_path.write_text(
        "思 probe(x) { x }\n"
        'ast = parse("思 加一(数) { 数 + 1 }")\n'
        "names = list_thoughts()\n"
        '{"ast_type": ast["type"], "ast_value": ast["value"], '
        '"has_probe": str_contains(str(names), "probe"), '
        '"has_parse_tokens": str_contains(str(names), "parse_tokens")}\n',
        encoding="utf-8",
    )

    result = subprocess.run(
        c_vm_cmd + ["--bootstrap", c_vm_path(c_vm_cmd, bootstrap_path), c_vm_path(c_vm_cmd, source_path)],
        capture_output=True,
        timeout=90,
    )
    stdout = result.stdout.decode("utf-8", "replace").strip()
    stderr = result.stderr.decode("utf-8", "replace")

    assert result.returncode == 0, stderr
    assert '"ast_type": "thought"' in stdout
    assert '"ast_value": "加一"' in stdout
    assert '"has_probe": true' in stdout
    assert '"has_parse_tokens": true' in stdout


def test_c_vm_bootstrap_cli_loads_inspect_source_using_parse_builtin(c_vm_cmd, tmp_path):
    bootstrap_path = ROOT / "demos" / "frontend_bootstrap.kub.json"
    source_path = tmp_path / "use_inspect.ku"
    source_path.write_text(
        '引 "std/inspect" 别 察\n'
        '{"refs": 察_refs("x + y"), "count": 察_count_nodes("x + y")}\n',
        encoding="utf-8",
    )

    result = subprocess.run(
        c_vm_cmd + ["--bootstrap", c_vm_path(c_vm_cmd, bootstrap_path), c_vm_path(c_vm_cmd, source_path)],
        capture_output=True,
        timeout=90,
    )
    stdout = result.stdout.decode("utf-8", "replace").strip()
    stderr = result.stderr.decode("utf-8", "replace")

    assert result.returncode == 0, stderr
    assert '"x"' in stdout
    assert '"y"' in stdout
    assert '"count": 3' in stdout


def test_c_vm_bootstrap_cli_loads_net_source_and_runs_core_builtins(c_vm_cmd, tmp_path):
    bootstrap_path = ROOT / "demos" / "frontend_bootstrap.kub.json"
    source_path = tmp_path / "use_net.ku"
    source_path.write_text(
        '引 "std/net" 别 网\n'
        '响应 = 网_取("https://example.test/天书?q=道")\n'
        '正文 = json_parse(响应["body"])\n'
        '{"ok": 响应["是否成功"], "status": 响应["状态码"], "method": 正文["method"], '
        '"url": 正文["url"], "decoded": url_decode(url_encode("天书/道"))}\n',
        encoding="utf-8",
    )

    result = subprocess.run(
        c_vm_cmd + ["--bootstrap", c_vm_path(c_vm_cmd, bootstrap_path), c_vm_path(c_vm_cmd, source_path)],
        capture_output=True,
        timeout=90,
    )
    stdout = result.stdout.decode("utf-8", "replace").strip()
    stderr = result.stderr.decode("utf-8", "replace")

    assert result.returncode == 0, stderr
    assert '"ok": true' in stdout
    assert '"status": 200' in stdout
    assert '"method": "GET"' in stdout
    assert '"url": "https://example.test/天书?q=道"' in stdout
    assert '"decoded": "天书/道"' in stdout


def test_c_vm_bootstrap_cli_supports_repeated_module_with_multiple_aliases(c_vm_cmd, tmp_path):
    bootstrap_path = ROOT / "demos" / "frontend_bootstrap.kub.json"
    source_path = tmp_path / "multi_alias.ku"
    source_path.write_text(
        '引 "std/math" 别 数\n'
        '引 "std/math.ku" 别 算\n'
        '{"fib": 数_斐波那契(10), "sum": 算_求和([1, 2, 3])}\n',
        encoding="utf-8",
    )

    result = subprocess.run(
        c_vm_cmd + ["--bootstrap", c_vm_path(c_vm_cmd, bootstrap_path), c_vm_path(c_vm_cmd, source_path)],
        capture_output=True,
        timeout=90,
    )
    stdout = result.stdout.decode("utf-8", "replace").strip()
    stderr = result.stderr.decode("utf-8", "replace")

    assert result.returncode == 0, stderr
    assert '"fib": 55' in stdout
    assert '"sum": 6' in stdout


def test_c_vm_bootstrap_cli_rejects_parent_directory_imports(c_vm_cmd, tmp_path):
    bootstrap_path = ROOT / "demos" / "frontend_bootstrap.kub.json"
    source_path = tmp_path / "bad_import.ku"
    source_path.write_text('引 "../test_core" 别 坏\n1\n', encoding="utf-8")

    result = subprocess.run(
        c_vm_cmd + ["--bootstrap", c_vm_path(c_vm_cmd, bootstrap_path), c_vm_path(c_vm_cmd, source_path)],
        capture_output=True,
        timeout=90,
    )
    stderr = result.stderr.decode("utf-8", "replace")

    assert result.returncode != 0
    assert "ImportError" in stderr
    assert "非法模块路径" in stderr


def test_c_vm_bootstrap_helper_builds_frontend_bytecode(c_vm_cmd, tmp_path):
    bootstrap_path = ROOT / "demos" / "frontend_bootstrap.kub.json"
    bootstrap_helper_path = ROOT / "dao" / "std" / "bootstrap.ku"
    source_path = tmp_path / "use_bootstrap_helper.ku"
    source_path.write_text(
        f'字节码 = 构造前端自举字节码("{c_vm_path(c_vm_cmd, ROOT)}")\n'
        '{"has_constants": has(字节码, "constants"), '
        '"has_instructions": has(字节码, "instructions"), '
        '"instr_count": len(字节码["instructions"])}\n',
        encoding="utf-8",
    )

    result = subprocess.run(
        c_vm_cmd + [
            "--bootstrap",
            c_vm_path(c_vm_cmd, bootstrap_path),
            c_vm_path(c_vm_cmd, bootstrap_helper_path),
            c_vm_path(c_vm_cmd, source_path),
        ],
        capture_output=True,
        timeout=90,
    )
    stdout = result.stdout.decode("utf-8", "replace").strip()
    stderr = result.stderr.decode("utf-8", "replace")

    assert result.returncode == 0, stderr
    assert '"has_constants": true' in stdout
    assert '"has_instructions": true' in stdout
    assert '"instr_count":' in stdout


def test_c_vm_bootstrap_helper_regenerates_usable_frontend_image(c_vm_cmd, tmp_path):
    bootstrap_path = ROOT / "demos" / "frontend_bootstrap.kub.json"
    bootstrap_helper_path = ROOT / "dao" / "std" / "bootstrap.ku"
    regenerated_path = tmp_path / "frontend_bootstrap.regenerated.kub.json"
    regen_source_path = tmp_path / "regenerate_frontend_bootstrap.ku"

    regen_source_path.write_text(
        f'输出路径 = "{c_vm_path(c_vm_cmd, regenerated_path)}"\n'
        f'字节码 = 写前端自举字节码("{c_vm_path(c_vm_cmd, ROOT)}", 输出路径)\n'
        '{"has_constants": has(字节码, "constants"), '
        '"has_instructions": has(字节码, "instructions"), '
        '"instr_count": len(字节码["instructions"])}\n',
        encoding="utf-8",
    )

    regenerate = subprocess.run(
        c_vm_cmd
        + [
            "--bootstrap",
            c_vm_path(c_vm_cmd, bootstrap_path),
            c_vm_path(c_vm_cmd, bootstrap_helper_path),
            c_vm_path(c_vm_cmd, regen_source_path),
        ],
        capture_output=True,
        timeout=120,
    )
    regen_stdout = regenerate.stdout.decode("utf-8", "replace").strip()
    regen_stderr = regenerate.stderr.decode("utf-8", "replace")

    assert regenerate.returncode == 0, regen_stderr
    assert regenerated_path.exists()
    assert '"has_constants": true' in regen_stdout
    assert '"has_instructions": true' in regen_stdout

    smoke_cases = [
        (
            "generated_bootstrap_smoke.ku",
            "思 加一(数) {\n"
            "  数 + 1\n"
            "}\n"
            "加一(41)\n",
            "42",
        ),
        (
            "generated_bootstrap_rich.ku",
            "思 求和二(左, 右) {\n"
            "  左 + 右\n"
            "}\n"
            "列表 = [1, 2, 3]\n"
            "字典 = {\"a\": 求和二(1, 2), \"b\": 列表[2]}\n"
            "i = 0\n"
            "总 = 0\n"
            "当 i < len(列表) {\n"
            "  总 = 总 + 列表[i]\n"
            "  i = i + 1\n"
            "}\n"
            "{\"sum\": 总, \"dict\": 字典}\n",
            '{"dict": {"b": 3, "a": 3}, "sum": 6}',
        ),
        (
            "generated_bootstrap_import.ku",
            '引 "std/math" 别 数\n'
            '{"fib": 数_斐波那契(10), "sum": 数_求和([1, 2, 3])}\n',
            '{"sum": 6, "fib": 55}',
        ),
    ]

    for filename, source, expected_stdout in smoke_cases:
        source_path = tmp_path / filename
        source_path.write_text(source, encoding="utf-8")
        smoke = subprocess.run(
            c_vm_cmd + ["--bootstrap", c_vm_path(c_vm_cmd, regenerated_path), c_vm_path(c_vm_cmd, source_path)],
            capture_output=True,
            timeout=120,
        )
        smoke_stdout = smoke.stdout.decode("utf-8", "replace").strip()
        smoke_stderr = smoke.stderr.decode("utf-8", "replace")

        assert smoke.returncode == 0, smoke_stderr
        assert smoke_stdout == expected_stdout


# ── Phase 1: type() 契约对齐门禁 ──────────────────────────────────────────
# 见 docs/C_VM_补完指导文书.md Phase 1。
# 覆盖 type()/is_int/is_str/is_list/is_dict/is_none 在 C VM 与 Python 间的一致性，
# 并显式记录由 C VM V_NUM(double) 统一数字类型导致的已知差异。


def _run_both(c_vm_cmd, ast):
    bytecode = compile_ast(ast)
    c_result = run_c_vm(c_vm_cmd, bytecode)
    python_result = DaoVM().execute(bytecode)
    return c_result, format_python_value(python_result)


@pytest.mark.parametrize(
    ("value", "expected"),
    [
        ("hello", '"str"'),
        ([], '"list"'),
        ({}, '"dict"'),
        (True, '"bool"'),
    ],
)
def test_c_vm_type_builtin_aligned_for_str_list_dict_bool(c_vm_cmd, value, expected):
    # 这些类型 C VM 和 Python 的 type() 返回值一致
    c_result, py_result = _run_both(c_vm_cmd, block(call_node("type", lit(value))))
    assert c_result == expected
    assert c_result == py_result


def test_c_vm_type_builtin_known_divergence_for_numbers(c_vm_cmd):
    # C VM 用 V_NUM(double) 统一存数字，type() 返回 "num"；
    # Python 区分 int/float，type() 返回 "int"/"float"。
    # 此差异由 ABI 决定，记录为门禁，待 C VM 引入 int/float 类型标记后消除。
    for val in (5, 5.0):
        c_result, py_result = _run_both(c_vm_cmd, block(call_node("type", lit(val))))
        assert c_result == '"num"', f"type({val}): C={c_result}"
        assert py_result in ('"int"', '"float"'), f"type({val}): Py={py_result}"


def test_c_vm_type_builtin_known_divergence_for_null(c_vm_cmd):
    c_result, py_result = _run_both(c_vm_cmd, block(call_node("type", lit(None))))
    assert c_result == '"nil"'
    assert py_result == '"NoneType"'


@pytest.mark.parametrize(
    ("fn", "value", "expected"),
    [
        ("is_str", "hello", "true"),
        ("is_str", 5, "false"),
        ("is_str", [1], "false"),
        ("is_list", [1, 2], "true"),
        ("is_list", "x", "false"),
        ("is_list", {}, "false"),
        ("is_dict", {}, "true"),
        ("is_dict", [1], "false"),
        ("is_dict", "x", "false"),
        ("is_none", None, "true"),
        ("is_none", 5, "false"),
        ("is_none", "x", "false"),
    ],
)
def test_c_vm_type_predicates_parity(c_vm_cmd, fn, value, expected):
    # is_str/is_list/is_dict/is_none 在 C VM 与 Python 间行为一致
    c_result, py_result = _run_both(c_vm_cmd, block(call_node(fn, lit(value))))
    assert c_result == expected, f"{fn}({value!r}): C={c_result}"
    assert c_result == py_result, f"{fn}({value!r}): C={c_result} Py={py_result}"


def test_c_vm_is_int_aligned_for_integral_value(c_vm_cmd):
    # is_int(5): C=true (整数值), Python=true (int 类型) — 一致
    c_result, py_result = _run_both(c_vm_cmd, block(call_node("is_int", lit(5))))
    assert c_result == "true"
    assert c_result == py_result


def test_c_vm_is_int_known_divergence_for_float_value(c_vm_cmd):
    # is_int(5.0): C VM 按整数值判断 → true；Python 按 int 类型判断 → false。
    # 由 V_NUM(double) ABI 决定，记录为门禁。
    c_result, py_result = _run_both(c_vm_cmd, block(call_node("is_int", lit(5.0))))
    assert c_result == "true", f"is_int(5.0) C={c_result}"
    assert py_result == "false", f"is_int(5.0) Py={py_result}"


def test_c_vm_type_ku_is_num_aligned_via_bootstrap(c_vm_cmd, tmp_path):
    # type.ku 的 is_num 用兼容表达式同时匹配 "num"(C) 和 "int"/"float"(Python)，
    # 在两侧都应返回 true。通过 bootstrap 加载 type.ku 验证。
    bootstrap_path = ROOT / "demos" / "frontend_bootstrap.kub.json"
    type_path = ROOT / "dao" / "std" / "type.ku"
    source = (
        '{"int": is_num(5), "float": is_num(5.0), "str": is_num("x"), '
        '"list": is_num([1]), "null": is_num(空)}\n'
    )
    source_path = tmp_path / "type_ku_is_num_probe.ku"
    source_path.write_text(source, encoding="utf-8")
    result = subprocess.run(
        c_vm_cmd
        + [
            "--bootstrap",
            c_vm_path(c_vm_cmd, bootstrap_path),
            c_vm_path(c_vm_cmd, type_path),
            c_vm_path(c_vm_cmd, source_path),
        ],
        capture_output=True,
        timeout=60,
    )
    stderr = result.stderr.decode("utf-8", "replace")
    stdout = result.stdout.decode("utf-8", "replace").strip()
    assert result.returncode == 0, stderr
    # is_num(5)=true, is_num(5.0)=true, is_num("x")=false, is_num([1])=false, is_num(空)=false
    # C VM dict 输出 key 顺序不保证，用 JSON 解析比较。
    expected = {"int": True, "float": True, "str": False, "list": False, "null": False}
    assert json.loads(stdout) == expected
