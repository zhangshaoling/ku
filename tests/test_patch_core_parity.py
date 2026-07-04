from dao.semantic_core import Patch
from semantic_test_utils import call_ku, load_ku_std_module, normalize_effect, normalize_empty


def normalize_patch(patch):
    return {
        "op": patch["op"],
        "path": patch["path"],
        "value": normalize_empty(patch.get("value")),
        "before": normalize_empty(patch.get("before")),
        "reason": patch["reason"],
    }


def test_ku_patch_replace_apply_matches_python_reference():
    load_ku_std_module("patch.ku")
    ast = {"type": "literal", "value": 1, "children": []}

    ku_patch = call_ku("补丁_替换", [["value"], 2, 1, "demo"])
    ku_changed = call_ku("补丁_应用", [ast, ku_patch])

    py_changed = Patch.replace(("value",), 2, before=1, reason="demo").apply(ast)

    assert ku_changed == py_changed
    assert ast["value"] == 1


def test_ku_patch_nested_add_and_remove_apply():
    load_ku_std_module("patch.ku")
    ast = {"type": "block", "children": [{"type": "literal", "value": 1}]}

    add_patch = call_ku("补丁_添加", [["children", 1], {"type": "literal", "value": 2}, "append"])
    added = call_ku("补丁_应用", [ast, add_patch])
    assert added["children"] == [{"type": "literal", "value": 1}, {"type": "literal", "value": 2}]
    assert ast["children"] == [{"type": "literal", "value": 1}]

    remove_patch = call_ku("补丁_删除", [["children", 0], {"type": "literal", "value": 1}, "drop"])
    removed = call_ku("补丁_应用", [added, remove_patch])
    assert removed["children"] == [{"type": "literal", "value": 2}]


def test_ku_patch_inverse_matches_python_reference_shape():
    load_ku_std_module("patch.ku")

    ku_patch = call_ku("补丁_替换", [["value"], 2, 1, "demo"])
    ku_inverse = call_ku("补丁_反向", [ku_patch])

    py_inverse = Patch.replace(("value",), 2, before=1, reason="demo").inverse()
    py_patch = {
        "op": py_inverse.op,
        "path": list(py_inverse.path),
        "value": py_inverse.value,
        "before": py_inverse.before,
        "reason": py_inverse.reason,
    }

    assert normalize_patch(ku_inverse) == normalize_patch(py_patch)


def test_ku_patch_to_effect_matches_python_reference_shape():
    load_ku_std_module("patch.ku")

    ku_patch = call_ku("补丁_替换", [["children", 0], {"type": "literal", "value": 2}, {}, "demo"])
    ku_effect = call_ku("补丁_转影响", [ku_patch])

    py_effect = Patch.replace(("children", 0), {"type": "literal", "value": 2}, before={}, reason="demo").to_effect().to_dict()

    assert normalize_effect(ku_effect) == normalize_effect(py_effect)
