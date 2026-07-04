from dao.semantic_core import Effect, Trace
from semantic_test_utils import call_ku, load_ku_std_module, normalize_effect, normalize_event


def test_ku_effect_matches_python_reference_shape():
    load_ku_std_module("trace.ku")

    ku_effect = call_ku("轨迹_调用影响", ["check_state", ["system.ready"]])
    py_effect = Effect("thought.call", "thought:check_state", ["system.ready"]).to_dict()

    assert normalize_effect(ku_effect) == normalize_effect(py_effect)


def test_ku_trace_record_matches_python_reference_event_shape():
    load_ku_std_module("trace.ku")

    ku_trace = call_ku("轨迹_新", ["trace-1"])
    ku_effect = call_ku("轨迹_结果影响", ["check_state"])
    ku_trace = call_ku("轨迹_记录成功", [ku_trace, ku_effect, "check_state", "ok"])

    py_trace = Trace("trace-1")
    py_trace.record(Effect("thought.result", "thought:check_state"), thought="check_state", result="ok")

    assert ku_trace["id"] == py_trace.to_dict()["id"]
    assert normalize_event(ku_trace["events"][0]) == normalize_event(py_trace.to_dict()["events"][0])


def test_ku_trace_records_error_event():
    load_ku_std_module("trace.ku")

    ku_trace = call_ku("轨迹_新", ["trace-err"])
    ku_effect = call_ku("轨迹_错误影响", ["missing", "not found"])
    ku_trace = call_ku("轨迹_记录失败", [ku_trace, ku_effect, "missing", "not found"])

    event = ku_trace["events"][0]
    assert event["ok"] is False
    assert event["effect"]["kind"] == "thought.error"
    assert event["effect"]["payload"] == {"error": "not found"}
