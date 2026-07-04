from dao.semantic_core import SemanticEnv, ToolSpec, thought_ast
from semantic_test_utils import call_ku, load_ku_std_module, normalize_event


def test_ku_env_define_thought_records_define_effect():
    load_ku_std_module("memory_env.ku")
    ast = thought_ast("check", ["verify"])

    ku_env = call_ku("环境_新", ["env-1"])
    ku_env = call_ku("环境_定义思", [ku_env, "check", ast])

    py_env = SemanticEnv()
    py_env.define_thought("check", ["verify"])

    assert ku_env["thoughts"]["check"] == ast
    assert normalize_event(ku_env["trace"]["events"][0]) == normalize_event(py_env.trace.to_dict()["events"][0])


def test_ku_env_memory_write_and_read_match_reference_events():
    load_ku_std_module("memory_env.ku")

    ku_env = call_ku("环境_新", ["env-2"])
    ku_env = call_ku("环境_记住", [ku_env, "goal", {"name": "stabilize"}, "session", {}])
    ku_result = call_ku("环境_忆起并记录", [ku_env, "goal", {}])

    py_env = SemanticEnv()
    py_env.remember("goal", {"name": "stabilize"})
    py_env.recall("goal", default={})

    assert ku_result["value"] == {"name": "stabilize"}
    assert ku_env["memories"]["goal"]["value"] == {"name": "stabilize"}
    assert normalize_event(ku_env["trace"]["events"][0]) == normalize_event(py_env.trace.to_dict()["events"][0])
    assert normalize_event(ku_result["env"]["trace"]["events"][1]) == normalize_event(py_env.trace.to_dict()["events"][1])


def test_ku_env_register_tool_matches_reference_event_shape():
    load_ku_std_module("memory_env.ku")

    ku_env = call_ku("环境_新", ["env-3"])
    ku_env = call_ku("环境_注册工具", [ku_env, "verify", "check state", "safe"])

    py_env = SemanticEnv()
    py_env.register_tool(ToolSpec("verify", lambda: "ok", description="check state", risk="safe"))

    assert ku_env["tools"]["verify"]["risk"] == "safe"
    assert normalize_event(ku_env["trace"]["events"][0]) == normalize_event(py_env.trace.to_dict()["events"][0])


def test_ku_env_missing_memory_read_records_failed_event():
    load_ku_std_module("memory_env.ku")

    ku_env = call_ku("环境_新", ["env-4"])
    ku_result = call_ku("环境_忆起并记录", [ku_env, "missing", "default"])

    assert ku_result["value"] == "default"
    event = ku_result["env"]["trace"]["events"][0]
    assert event["effect"]["kind"] == "memory.read"
    assert event["ok"] is False
