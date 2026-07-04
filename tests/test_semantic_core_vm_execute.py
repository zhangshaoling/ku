from pathlib import Path

from dao.compiler import DaoVM
from dao.dao_lexer import lex
from dao.dao_parser import parse_tokens
from dao.runtime import DaoEnv, Thought
from semantic_test_utils import call_ku, load_ku_std_module, normalize_effect, normalize_value


ROOT = Path(__file__).resolve().parents[1]


def parse_file(path):
    tokens = lex(path.read_text(encoding="utf-8"))
    tokens.append({"type": "eof", "value": "", "line": 0, "col": 0, "pos": 0})
    return parse_tokens(tokens)


def lit(value):
    return {"type": "literal", "value": value, "children": []}


def ref(name):
    return {"type": "ref", "value": name, "children": []}


def assign(name, value):
    return {"type": "assign", "value": name, "children": [value]}


def block_node(*items):
    return {"type": "block", "children": list(items)}


def list_node(*items):
    return {"type": "list", "children": list(items)}


def dict_node(values):
    return {
        "type": "dict",
        "children": [
            {"type": "pair", "value": key, "children": [expr_node(value)]}
            for key, value in values.items()
        ],
    }


def expr_node(value):
    if isinstance(value, dict):
        return dict_node(value)
    if isinstance(value, list):
        return list_node(*(expr_node(item) for item in value))
    return lit(value)


def call_node(name, *args):
    return {"type": "call", "value": name, "children": list(args)}


def run_vm_module_call(module_name, call_ast):
    Thought.registry.clear()
    env = DaoEnv()
    env.load(str(ROOT / "dao" / "std" / "compiler.ku"))
    ast = parse_file(ROOT / "dao" / "std" / module_name)
    ast["children"].append(call_ast)
    bytecode = Thought.registry["compile_ast"].call([ast])
    return DaoVM().execute(bytecode)


def test_trace_core_function_executes_in_vm():
    vm_effect = run_vm_module_call(
        "trace.ku",
        call_node("轨迹_调用影响", lit("check_state"), list_node(lit("system.ready"))),
    )

    load_ku_std_module("trace.ku")
    ku_effect = call_ku("轨迹_调用影响", ["check_state", ["system.ready"]])

    assert normalize_effect(vm_effect) == normalize_effect(ku_effect)


def test_patch_core_function_executes_in_vm():
    vm_effect = run_vm_module_call(
        "patch.ku",
        call_node(
            "补丁_转影响",
            call_node(
                "补丁_替换",
                list_node(lit("children"), lit(0)),
                dict_node({"type": "literal", "value": 2}),
                dict_node({}),
                lit("demo"),
            ),
        ),
    )

    load_ku_std_module("patch.ku")
    ku_patch = call_ku("补丁_替换", [["children", 0], {"type": "literal", "value": 2}, {}, "demo"])
    ku_effect = call_ku("补丁_转影响", [ku_patch])

    assert normalize_effect(vm_effect) == normalize_effect(ku_effect)


def test_memory_env_core_function_executes_in_vm():
    vm_env = run_vm_module_call("memory_env.ku", call_node("环境_新", lit("env-1")))

    load_ku_std_module("memory_env.ku")
    ku_env = call_ku("环境_新", ["env-1"])

    assert normalize_value(vm_env) == normalize_value(ku_env)


def test_memory_env_define_thought_executes_in_vm():
    thought_ast = {"type": "literal", "value": "ok", "children": []}
    vm_env = run_vm_module_call(
        "memory_env.ku",
        block_node(
            assign("env", call_node("环境_新", lit("env-define"))),
            call_node("环境_定义思", ref("env"), lit("check"), expr_node(thought_ast)),
        ),
    )

    load_ku_std_module("memory_env.ku")
    ku_env = call_ku("环境_新", ["env-define"])
    ku_env = call_ku("环境_定义思", [ku_env, "check", thought_ast])

    assert normalize_value(vm_env) == normalize_value(ku_env)


def test_memory_env_memory_write_executes_in_vm():
    value = {"name": "stabilize"}
    vm_env = run_vm_module_call(
        "memory_env.ku",
        block_node(
            assign("env", call_node("环境_新", lit("env-memory"))),
            call_node("环境_记住", ref("env"), lit("goal"), expr_node(value), lit("session"), dict_node({})),
        ),
    )

    load_ku_std_module("memory_env.ku")
    ku_env = call_ku("环境_新", ["env-memory"])
    ku_env = call_ku("环境_记住", [ku_env, "goal", value, "session", {}])

    assert normalize_value(vm_env) == normalize_value(ku_env)


def test_memory_env_tool_register_executes_in_vm():
    vm_env = run_vm_module_call(
        "memory_env.ku",
        block_node(
            assign("env", call_node("环境_新", lit("env-tool"))),
            call_node("环境_注册工具", ref("env"), lit("verify"), lit("check state"), lit("safe")),
        ),
    )

    load_ku_std_module("memory_env.ku")
    ku_env = call_ku("环境_新", ["env-tool"])
    ku_env = call_ku("环境_注册工具", [ku_env, "verify", "check state", "safe"])

    assert normalize_value(vm_env) == normalize_value(ku_env)
