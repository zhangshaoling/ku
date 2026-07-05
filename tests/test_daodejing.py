"""Tests for dao/std/daodejing.ku — the 81-chapter Dao De Jing transform core.

Each chapter is a pure function f(input) -> output. Tests verify:
- Individual chapter transforms compose correctly
- The combinators (道生, 三生万物, 反) chain chapters
- Sequences of chapters produce expected outputs
"""

import os
import re
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent


def _cvm_cmd() -> list[str]:
    env_val = os.environ.get("DAO_CORE_EXE")
    if env_val and Path(env_val).exists():
        return [env_val]
    compiled = ROOT / "dao" / "dao_core.exe"
    if compiled.exists():
        return [str(compiled)]
    pytest.skip("dao_core.exe not found; set DAO_CORE_EXE or build the C VM")


def _run(source: str) -> tuple[int, str, str]:
    env = os.environ.copy()
    env["DAO_GC_STATS"] = "0"  # keep output clean
    # Write source to a temp file in the project root so C VM can resolve imports
    tmp_ku = ROOT / "_test_daodejing_tmp.ku"
    tmp_ku.write_text(source, encoding="utf-8")
    try:
        result = subprocess.run(
            _cvm_cmd() + ["--bootstrap", str(ROOT / "demos" / "frontend_bootstrap.kub.json"), str(tmp_ku)],
            capture_output=True,
            timeout=120,
            env=env,
        )
        stdout = result.stdout.decode("utf-8", "replace").strip()
        stderr = result.stderr.decode("utf-8", "replace").strip()
        return result.returncode, stdout, stderr
    finally:
        tmp_ku.unlink(missing_ok=True)


# ── Individual chapters ──────────────────────────────────────────────

class TestDaoDeJingChapters:
    """Sample tests for key chapters."""

    def test_ch1_wraps_with_underscore(self):
        rc, stdout, stderr = _run('引 "std/daodejing" 别 道\nprint(道_第1章("天地"))\n')
        assert rc == 0, stderr
        assert "_天地_" in stdout

    def test_ch2_splits_into_yin_yang(self):
        rc, stdout, stderr = _run('引 "std/daodejing" 别 道\nprint(道_第2章([10,20,30,40]))\n')
        assert rc == 0, stderr
        assert "阴" in stdout
        assert "阳" in stdout

    def test_ch4_dilutes_number(self):
        rc, stdout, stderr = _run('引 "std/daodejing" 别 道\nprint(道_第4章(100))\n')
        assert rc == 0, stderr
        assert "50" in stdout

    def test_ch8_water_subtracts_one(self):
        rc, stdout, stderr = _run('引 "std/daodejing" 别 道\nprint(道_第8章(100))\n')
        assert rc == 0, stderr
        assert "99" in stdout

    def test_ch22_reverses_sign(self):
        rc, stdout, stderr = _run('引 "std/daodejing" 别 道\nprint(道_第22章(42))\n')
        assert rc == 0, stderr
        assert "-42" in stdout

    def test_ch25_wraps_in_four_domains(self):
        rc, stdout, stderr = _run('引 "std/daodejing" 别 道\nprint(道_第25章("x"))\n')
        assert rc == 0, stderr
        assert "道" in stdout
        assert "天" in stdout
        assert "地" in stdout
        assert "王" in stdout

    def test_ch42_generates_sequence(self):
        rc, stdout, stderr = _run('引 "std/daodejing" 别 道\nprint(道_第42章(1))\n')
        assert rc == 0, stderr
        assert "1" in stdout
        assert "2" in stdout
        assert "3" in stdout

    def test_ch77_balances_around_100(self):
        rc, stdout, stderr = _run('引 "std/daodejing" 别 道\nprint(道_第77章(150))\nprint(道_第77章(50))\n')
        assert rc == 0, stderr
        # 150 > 100 → 150-50=100; 50 <= 100 → 50+50=100
        assert stdout.count("100") >= 2

    def test_ch39_returns_one(self):
        rc, stdout, stderr = _run('引 "std/daodejing" 别 道\nprint(道_第39章("anything"))\n')
        assert rc == 0, stderr
        assert "1" in stdout


# ── Combinators ──────────────────────────────────────────────────────

class TestCombinators:
    """Test the chapter combinators (道生, 三生万物, 反)."""

    def test_dao_sheng_sequence(self):
        """Apply chapters 1, 37, 81 in sequence."""
        rc, stdout, stderr = _run(
            '引 "std/daodejing" 别 道\nprint(道_道生("_x_", [1, 37, 81]))\n'
        )
        assert rc == 0, stderr
        # _x_ → Ch1 → "_x_" → Ch37 → "_x_" → Ch81 → "信"(always)
        assert "信" in stdout

    def test_three_begets_all(self):
        """三生万物 with 4 input values."""
        rc, stdout, stderr = _run(
            '引 "std/daodejing" 别 道\nprint(道_三生万物(10, 42, 77, 81))\n'
        )
        assert rc == 0, stderr
        assert "信" in stdout  # final chapter 81 always returns "信"

    def test_reversal_sequence(self):
        """反 reverses chapter order."""
        rc, stdout, stderr = _run(
            '引 "std/daodejing" 别 道\nprint(道_反("正", [1, 8, 22]))\n'
        )
        assert rc == 0, stderr
        # "正" → Ch22 (reverse) → doesn't affect string → stays "正"
        # Then Ch8 → doesn't affect string → stays "正"
        # Then Ch1 → wraps → "_正_"
        assert "_正_" in stdout


# ── Full pipeline ────────────────────────────────────────────────────

class TestFullPipeline:
    """End-to-end pipeline test."""

    def test_full_81_chapter_system_loads(self):
        """Import all 81 chapters without error."""
        rc, stdout, stderr = _run(
            '引 "std/daodejing" 别 道\nprint("loaded")\n'
        )
        assert rc == 0, stderr
        assert "loaded" in stdout

    def test_chaining_many_chapters(self):
        """Chain 5 chapters and verify output."""
        seq = [1, 4, 8, 22, 81]
        rc, stdout, stderr = _run(
            '引 "std/daodejing" 别 道\n'
            f'print(道_道生("seed", {seq}))\n'
        )
        assert rc == 0, stderr
        # After Ch81, output is always "信"
        assert "信" in stdout
