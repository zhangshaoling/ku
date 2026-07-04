from dao.semantic_core import thought_ast
from semantic_test_utils import call_ku, load_ku_std_module, normalize_value


def test_ku_constructs_same_short_thought_ast_as_python_reference():
    load_ku_std_module("semantic_core.ku")
    steps = [
        "observe tests.fail",
        {"locate": {"mode": "causal"}},
        {"patch": {"scope": "minimal"}},
        "verify",
    ]

    py_ast = thought_ast("fix_bug", steps)
    ku_ast = call_ku("语义_构造无参思", ["fix_bug", steps])

    assert normalize_value(ku_ast) == normalize_value(py_ast)


def test_ku_value_to_node_matches_reference_shape_for_data():
    load_ku_std_module("semantic_core.ku")

    node = call_ku("语义_值转节点", [{"scope": "minimal", "count": 2}])
    norm = normalize_value(node)

    assert norm["type"] == "dict"
    assert norm["value"] == ""
    pairs = {pair["value"]: pair["children"][0]["value"] for pair in norm["children"]}
    assert pairs == {"scope": "minimal", "count": 2}
