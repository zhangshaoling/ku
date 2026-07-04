import subprocess
from pathlib import Path

import pytest

from dao.c_vm_runtime import CVMRuntime, DEFAULT_BINARY, ROOT


@pytest.fixture(scope="module", autouse=True)
def build_c_vm():
    result = subprocess.run(
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
    if result.returncode != 0 or not DEFAULT_BINARY.exists():
        stderr = result.stderr.decode("utf-8", "replace") if isinstance(result.stderr, bytes) else str(result.stderr)
        pytest.skip(f"native C VM is not available: {stderr}")


@pytest.fixture()
def runtime():
    return CVMRuntime(timeout=60)


def assert_ok(result):
    assert result.ok, result.error or result.stderr or result.stdout
    return result.value


def test_run_source_frontend_expression(runtime):
    result = runtime.run_source("1 + 2", profile="frontend")
    assert assert_ok(result) == 3
    assert result.command
    assert "dao_core.exe" in Path(result.command[0]).name


def test_run_source_multiline_thought(runtime):
    result = runtime.run_source("思 加一(x) { x + 1 }\n加一(41)", profile="frontend")
    assert assert_ok(result) == 42


def test_call_thought_core_profile(runtime):
    result = runtime.call_thought("求和", [[1, 2, 3]], params=["列表"], profile="core")
    assert assert_ok(result) == 6


def test_call_thought_preserves_string_args(runtime):
    result = runtime.call_thought("is_numeric", ["12345"], params=["s"], profile="core")
    assert assert_ok(result) is True


def test_memory_profile_experience_roundtrip(runtime, tmp_path):
    data_dir = tmp_path / "dao_data"

    recorded = runtime.call_thought(
        "gap_record",
        ["M4.5", "runtime gateway test", "needs C VM memory", "keep going", "gateway,memory"],
        params=["topic", "context", "missing", "next_action", "tags"],
        profile="memory",
        data_dir=data_dir,
    )
    value = assert_ok(recorded)
    assert value["kind"] == "gap"
    assert value["status"] == "open"
    assert (data_dir / "experience.db").exists()

    listed = runtime.call_thought("gap_list_open", [20], params=["limit"], profile="memory", data_dir=data_dir)
    gaps = assert_ok(listed)
    assert gaps["count"] >= 1
    assert any(row["topic"] == "M4.5" for row in gaps["gaps"])


def test_runtime_error_shape_for_unknown_thought(runtime):
    result = runtime.call_thought("不存在的思", [], params=[], profile="frontend")
    assert not result.ok
    assert result.error
    assert "NameError" in result.error
