"""M4 memory-ownership regression tests.

Verify that the C VM arena allocator leaks zero Val / Env / Frame objects
across a range of execution paths (.ku source, kub.json bytecode, bootstrap).

The C VM prints a leak report to stderr when DAO_GC_STATS=1:
    [dao-gc] val: alloc=X free=X leak=X | env: alloc=X free=X leak=X | frame: alloc=X free=X leak=X
"""

import os
import re
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent

GC_RE = re.compile(
    r"val:\s+alloc=(\d+)\s+free=(\d+)\s+leak=(\d+)\s+\|\s+"
    r"env:\s+alloc=(\d+)\s+free=(\d+)\s+leak=(\d+)\s+\|\s+"
    r"frame:\s+alloc=(\d+)\s+free=(\d+)\s+leak=(\d+)"
)


def _cvm_cmd() -> list[str]:
    env_val = os.environ.get("DAO_CORE_EXE")
    if env_val and Path(env_val).exists():
        return [env_val]
    compiled = ROOT / "dao" / "dao_core.exe"
    if compiled.exists():
        return [str(compiled)]
    pytest.skip("dao_core.exe not found; set DAO_CORE_EXE or build the C VM")


def _run_with_gc_stats(cmd: list[str]) -> tuple[str, str]:
    env = os.environ.copy()
    env["DAO_GC_STATS"] = "1"
    result = subprocess.run(cmd, capture_output=True, timeout=120, env=env)
    stdout = result.stdout.decode("utf-8", "replace")
    stderr = result.stderr.decode("utf-8", "replace")
    return stdout, stderr


def _assert_zero_leak(stderr: str, label: str) -> None:
    m = GC_RE.search(stderr)
    assert m is not None, f"[{label}] no [dao-gc] report in stderr:\n{stderr[-500:]}"
    v_al, v_fr, v_lk, e_al, e_fr, e_lk, f_al, f_fr, f_lk = (int(x) for x in m.groups())
    assert v_al > 0, f"[{label}] no val allocations observed; stderr tail: {stderr[-100:]}"
    assert v_lk == 0, f"[{label}] val leak={v_lk}\n{stderr[-200:]}"
    assert e_lk == 0, f"[{label}] env leak={e_lk}\n{stderr[-200:]}"
    assert f_lk == 0, f"[{label}] frame leak={f_lk}\n{stderr[-200:]}"
    assert v_al == v_fr, f"[{label}] val alloc={v_al} free={v_fr}"
    assert e_al == e_fr, f"[{label}] env alloc={e_al} free={e_fr}"
    assert f_al == f_fr, f"[{label}] frame alloc={f_al} free={f_fr}"


# --- direct .ku bytecode ---

def test_golden_path_leaks_zero() -> None:
    cmd = _cvm_cmd() + [str(ROOT / "demos" / "golden_path.ku")]
    _, stderr = _run_with_gc_stats(cmd)
    _assert_zero_leak(stderr, "golden_path.ku")


def test_semantic_std_combo_leaks_zero() -> None:
    cmd = _cvm_cmd() + [str(ROOT / "demos" / "semantic_std_combo.kub.json")]
    _, stderr = _run_with_gc_stats(cmd)
    _assert_zero_leak(stderr, "semantic_std_combo")


def test_frontend_compile_demo_leaks_zero() -> None:
    cmd = _cvm_cmd() + [str(ROOT / "demos" / "frontend_compile_demo.kub.json")]
    _, stderr = _run_with_gc_stats(cmd)
    _assert_zero_leak(stderr, "frontend_compile_demo")


# --- bootstrap path (MAKE_FUNCTION, closures) ---

def test_bootstrap_golden_path_leaks_zero() -> None:
    bootstrap = ROOT / "demos" / "frontend_bootstrap.kub.json"
    program = ROOT / "demos" / "golden_path.ku"
    if not program.exists():
        pytest.skip(f"program missing: {program}")
    cmd = _cvm_cmd() + ["--bootstrap", str(bootstrap), str(program)]
    _, stderr = _run_with_gc_stats(cmd)
    _assert_zero_leak(stderr, "bootstrap+golden_path")


def test_bootstrap_helper_constructs_bytecode_leaks_zero() -> None:
    """Exercise MAKE_FUNCTION / compile path explicitly via bootstrap.ku helper."""
    bootstrap = ROOT / "demos" / "frontend_bootstrap.kub.json"
    helper = ROOT / "dao" / "std" / "bootstrap.ku"
    prog = ROOT / "demos" / "golden_path.ku"
    if not helper.exists():
        pytest.skip("dao/std/bootstrap.ku missing")
    cmd = _cvm_cmd() + ["--bootstrap", str(bootstrap), str(helper), str(prog)]
    _, stderr = _run_with_gc_stats(cmd)
    _assert_zero_leak(stderr, "bootstrap+helper+program")


# --- repeated loop-pump regression (no accumulation) ---

def test_repeated_executions_do_not_accumulate() -> None:
    bootstrap = ROOT / "demos" / "frontend_bootstrap.kub.json"
    program = ROOT / "demos" / "golden_path.ku"
    if not bootstrap.exists() or not program.exists():
        pytest.skip("missing demo files")
    counts: list[tuple[int, int, int]] = []
    for i in range(3):
        cmd = _cvm_cmd() + ["--bootstrap", str(bootstrap), str(program)]
        _, stderr = _run_with_gc_stats(cmd)
        m = GC_RE.search(stderr)
        assert m is not None, f"no gc report at iteration {i}:\n{stderr[-200:]}"
        v_lk, e_lk, f_lk = int(m.group(3)), int(m.group(6)), int(m.group(9))
        counts.append((v_lk, e_lk, f_lk))
    for i, (vl, el, fl) in enumerate(counts):
        assert vl == 0 and el == 0 and fl == 0, (
            f"iteration {i}: non-zero leak: val={vl} env={el} frame={fl}"
        )
