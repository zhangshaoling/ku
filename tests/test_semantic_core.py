from dao.runtime import DaoEnv, Thought
from dao.semantic_core import (
    DaoSemanticAdapter,
    Effect,
    Patch,
    SemanticEnv,
    ToolSpec,
    thought_ast,
    thought_from_ast,
)


def _call_name(node):
    return node["children"][0]["value"]


def test_short_thought_expands_to_canonical_ast():
    ast = thought_ast(
        "fix_bug",
        [
            "observe tests.fail",
            {"locate": {"mode": "causal"}},
            {"patch": {"scope": "minimal"}},
            "verify",
        ],
    )

    assert ast["type"] == "thought"
    assert ast["value"] == "fix_bug"

    body = ast["children"][-1]
    calls = body["children"]
    assert [_call_name(call) for call in calls] == ["observe", "locate", "patch", "verify"]
    assert calls[0]["children"][1]["value"] == "tests.fail"

    locate_arg = calls[1]["children"][1]
    assert locate_arg["type"] == "dict"
    assert locate_arg["children"][0]["value"] == "mode"
    assert locate_arg["children"][0]["children"][0]["value"] == "causal"


def test_patch_is_pure_and_reversible():
    ast = {"type": "literal", "value": 1, "children": []}
    patch = Patch.replace(("value",), 2, before=1, reason="demo")

    changed = patch.apply(ast)

    assert ast["value"] == 1
    assert changed["value"] == 2
    assert patch.inverse().apply(changed) == ast


def test_semantic_env_records_memory_tools_and_trace():
    env = SemanticEnv()
    env.remember("goal", {"name": "stabilize"})
    assert env.recall("goal") == {"name": "stabilize"}

    env.register_tool(ToolSpec("double", lambda value: value * 2))
    assert env.call_tool("double", [3]) == 6

    env.define_thought("check", ["observe state", "verify"])

    effects = [event.effect.kind for event in env.trace.events]
    assert effects == [
        "memory.write",
        "memory.read",
        "tool.register",
        "tool",
        "define",
    ]


def test_patch_can_be_recorded_as_effect():
    patch = Patch.replace(("children", 0), {"type": "literal", "value": 2}, before={})
    effect = patch.to_effect()

    assert isinstance(effect, Effect)
    assert effect.kind == "patch"
    assert effect.target == "children.0"


def test_thought_from_ast_can_avoid_global_registration():
    Thought.registry.clear()
    ast = thought_ast("probe", ["verify"])

    thought = thought_from_ast(ast, register=False)

    assert thought.name == "probe"
    assert thought.params == []
    assert "probe" not in Thought.registry


def test_dao_semantic_adapter_registers_executable_short_thought():
    Thought.registry.clear()
    dao_env = DaoEnv()
    adapter = DaoSemanticAdapter(dao_env)
    observed = []

    adapter.register_tool(ToolSpec("observe", lambda target: observed.append(target) or target))
    adapter.register_tool(ToolSpec("verify", lambda: "ok"))

    thought = adapter.define_thought("check_state", ["observe system.ready", "verify"])

    assert isinstance(thought, Thought)
    assert dao_env.run("check_state", []) == "ok"
    assert observed == ["system.ready"]

    effects = [event.effect.kind for event in adapter.semantic_env.trace.events]
    assert "dao.register" in effects
    assert effects.count("tool") == 2


def test_adapter_run_thought_records_call_and_result():
    Thought.registry.clear()
    dao_env = DaoEnv()
    adapter = DaoSemanticAdapter(dao_env)

    adapter.register_tool(ToolSpec("verify", lambda: "ok"))
    adapter.define_thought("check", ["verify"])

    assert adapter.run_thought("check") == "ok"

    effects = [event.effect.kind for event in adapter.semantic_env.trace.events]
    assert effects[-3:] == ["thought.call", "tool", "thought.result"]
    assert adapter.semantic_env.trace.events[-1].result == "ok"


def test_adapter_run_thought_records_errors():
    Thought.registry.clear()
    dao_env = DaoEnv()
    adapter = DaoSemanticAdapter(dao_env)

    try:
        adapter.run_thought("missing")
    except KeyError:
        pass
    else:
        raise AssertionError("missing thought should raise KeyError")

    event = adapter.semantic_env.trace.events[-1]
    assert event.effect.kind == "thought.error"
    assert event.ok is False
