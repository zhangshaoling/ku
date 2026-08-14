"""std/test_runner.py — 道语言 std/test_runner.ku 的 Python reference 实现

按 docs/AGI母语语义内核规范.md #16 收敛原则:
  - 本文件冻结为 reference/bootstrap, 不再承载新语义能力
  - 仅用于 byte-equal 对拍: Python DaoVM 编译的字节码 vs C VM 编译的字节码
  - 不允许新增必须依赖 Python 对象才能表达的语义

道语言 std/test_runner.ku 的接口:
  tr_注册(名, 函数) -> 套件
  tr_重置() -> 套件
  断言_相等(实际, 期望, 信息) -> 布尔 (不返回错误, 只登记)
  断言_真(条件, 信息) -> 布尔
  断言_假(条件, 信息) -> 布尔
  断言_不等(实际, 期望, 信息) -> 布尔
  断言_近似(a, b, 容差, 信息) -> 布尔
  断言_长度(列表, 期望长度, 信息) -> 布尔
  断言_包含(列表, 元素, 信息) -> 布尔
  跑() -> 报告
  跑并打印() -> 报告
"""

from typing import Any, Callable, Optional


class _Suite:
    """测试套件状态, 镜像 __tr_suite dict."""

    def __init__(self) -> None:
        self.tests: list[dict[str, Any]] = []
        self.passed: int = 0
        self.failed: int = 0
        self.errors: list[str] = []


# 全局套件, 镜像 __tr_suite 全局变量
_SUITE = _Suite()


def tr_注册(名: str, 函数: Optional[Callable[[], None]]) -> dict:
    """注册一个测试用例.

    Returns: 套件 dict, 与 .ku 版的 __tr_suite 形状对齐.
    """
    if 名 is None:
        名 = ""
    if 函数 is None:
        函数 = None
    _SUITE.tests.append({"name": 名, "fn": 函数})
    return {
        "tests": _SUITE.tests,
        "passed": _SUITE.passed,
        "failed": _SUITE.failed,
        "errors": _SUITE.errors,
    }


def tr_重置() -> dict:
    _SUITE.tests = []
    _SUITE.passed = 0
    _SUITE.failed = 0
    _SUITE.errors = []
    return {
        "tests": _SUITE.tests,
        "passed": _SUITE.passed,
        "failed": _SUITE.failed,
        "errors": _SUITE.errors,
    }


def _记失败(msg: str) -> str:
    _SUITE.errors.append(msg)
    _SUITE.failed += 1
    return msg


def _记通过() -> bool:
    _SUITE.passed += 1
    return True


def 断言_相等(实际: Any, 期望: Any, 信息: str = "") -> bool:
    if 信息 is None:
        信息 = ""
    if 实际 == 期望:
        return _记通过()
    msg = "ASSERT_EQUAL 失败: 期望 " + str(期望) + ", 实际 " + str(实际)
    if 信息 != "":
        msg = msg + " (" + 信息 + ")"
    return _记失败(msg) is not None  # type: ignore[return-value]


def 断言_真(条件: Any, 信息: str = "") -> bool:
    return 断言_相等(条件, True, 信息)


def 断言_假(条件: Any, 信息: str = "") -> bool:
    return 断言_相等(条件, False, 信息)


def 断言_不等(实际: Any, 期望: Any, 信息: str = "") -> bool:
    if 信息 is None:
        信息 = ""
    if 实际 != 期望:
        return _记通过()
    msg = "ASSERT_NOT_EQUAL 失败: 不应等于 " + str(期望)
    if 信息 != "":
        msg = msg + " (" + 信息 + ")"
    return _记失败(msg) is not None  # type: ignore[return-value]


def 断言_近似(a: float, b: float, 容差: float = 0.0001, 信息: str = "") -> bool:
    if 容差 is None:
        容差 = 0.0001
    if 信息 is None:
        信息 = ""
    diff = abs(a - b)
    if diff <= 容差:
        return _记通过()
    msg = "ASSERT_NEAR 失败: 期望 " + str(b) + " ± " + str(容差) + ", 实际 " + str(a)
    if 信息 != "":
        msg = msg + " (" + 信息 + ")"
    return _记失败(msg) is not None  # type: ignore[return-value]


def 断言_长度(列表: list, 期望长度: int, 信息: str = "") -> bool:
    return 断言_相等(len(列表), 期望长度, 信息)


def 断言_包含(列表: list, 元素: Any, 信息: str = "") -> bool:
    if 信息 is None:
        信息 = ""
    found = False
    for x in 列表:
        if x == 元素:
            found = True
            break
    if found:
        return _记通过()
    msg = "ASSERT_CONTAINS 失败: 列表中无 " + str(元素)
    if 信息 != "":
        msg = msg + " (" + 信息 + ")"
    return _记失败(msg) is not None  # type: ignore[return-value]


def 跑() -> dict:
    saved_passed = _SUITE.passed
    saved_failed = _SUITE.failed
    saved_errors = list(_SUITE.errors)

    本次通过 = 0
    本次失败 = 0
    本次错误: list[str] = []
    results = []

    for t in _SUITE.tests:
        name = t["name"]
        fn = t["fn"]
        result_item: dict[str, Any] = {"name": name, "passed": True, "errors": []}

        if fn is None:
            result_item["passed"] = False
            result_item["errors"] = ["fn is 空"]
        else:
            _SUITE.passed = 0
            _SUITE.failed = 0
            _SUITE.errors = []
            try:
                fn()
            except Exception as e:
                _SUITE.failed += 1
                _SUITE.errors.append("EXCEPTION: " + repr(e))
            子通过 = _SUITE.passed
            子失败 = _SUITE.failed
            子错误 = list(_SUITE.errors)

        if 子失败 > 0:
            result_item["passed"] = False
            result_item["errors"] = 子错误
            本次失败 += 1
        else:
            本次通过 += 1
        results.append(result_item)

    _SUITE.passed = saved_passed + 本次通过
    _SUITE.failed = saved_failed + 本次失败
    for e in 本次错误:
        _SUITE.errors.append(e)

    return {
        "results": results,
        "total": len(results),
        "passed": 本次通过,
        "failed": 本次失败,
        "all_passed": 本次失败 == 0,
    }


def 跑并打印() -> dict:
    r = 跑()
    print("═══════════════════════════════════")
    print("  道语言测试报告")
    print("═══════════════════════════════════")
    print("  总数: " + str(r["total"]))
    print("  通过: " + str(r["passed"]))
    print("  失败: " + str(r["failed"]))
    print("───────────────────────────────────")
    for item in r["results"]:
        if item["passed"]:
            print("  ✓ " + item["name"])
        else:
            print("  ✗ " + item["name"])
            for e in item["errors"]:
                print("      " + e)
    print("═══════════════════════════════════")
    if r["all_passed"]:
        print("  ALL PASSED")
    else:
        print("  HAS FAILURES")
    print("═══════════════════════════════════")
    return r
