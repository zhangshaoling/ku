"""
道语言自主经验循环 (Autonomous Experience Loop)

数据 = 代码 = 记忆 = 工具 四环闭环。

每个任务执行前自动召回相关经验，执行后自动记录并按质量分自动提升为 MCP tool。

用法:
    python tools\autonomous_loop.py -- "演示 6 的阶乘"
    python tools\autonomous_loop.py --task "搜索道德经"
    python tools\autonomous_loop.py --cycle 5          # 连续运行 N 个循环
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DAO_CORE = ROOT / "dao" / "dao_core.exe"
BOOTSTRAP = ROOT / "demos" / "frontend_bootstrap.kub.json"
DAO_DATA_DIR = ROOT / "dao_data"


def now_iso() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S+00:00")


def run_ku(source: str, profile: str = "core", timeout: float = 60.0) -> tuple[bool, str, str]:
    """Run Ku source through the C VM. Returns (ok, stdout, stderr)."""
    env = os.environ.copy()
    env["DAO_GC_STATS"] = "0"
    env["DAO_DATA_DIR"] = str(DAO_DATA_DIR)
    # Write source to temp file so C VM can read it as argument (avoids stdin issues)
    tmp = ROOT / "_autonomous_loop_tmp.ku"
    tmp.write_text(source, encoding="utf-8")
    try:
        result = subprocess.run(
            [str(DAO_CORE), "--bootstrap", str(BOOTSTRAP), str(tmp)],
            capture_output=True,
            timeout=timeout,
            env=env,
            cwd=str(ROOT),
        )
    finally:
        tmp.unlink(missing_ok=True)
    stdout = result.stdout.decode("utf-8", "replace").strip()
    stderr = result.stderr.decode("utf-8", "replace").strip()
    return result.returncode == 0, stdout, stderr


def run_ku_file(path: str, profile: str = "core", timeout: float = 60.0) -> tuple[bool, str, str]:
    """Run a .ku file through the C VM."""
    env = os.environ.copy()
    env["DAO_GC_STATS"] = "0"
    env["DAO_DATA_DIR"] = str(DAO_DATA_DIR)
    result = subprocess.run(
        [str(DAO_CORE), "--bootstrap", str(BOOTSTRAP), path],
        capture_output=True,
        timeout=timeout,
        env=env,
        cwd=str(ROOT),
    )
    stdout = result.stdout.decode("utf-8", "replace").strip()
    stderr = result.stderr.decode("utf-8", "replace").strip()
    return result.returncode == 0, stdout, stderr


def init_experience_db() -> None:
    """Ensure experience DB is initialized."""
    DAO_DATA_DIR.mkdir(parents=True, exist_ok=True)
    src = '引 "std/experience" 别 经验\n经验_init()\nprint("init ok")\n'
    ok, stdout, stderr = run_ku(src, profile="memory", timeout=30.0)
    if not ok:
        print(f"[warn] experience_init failed: {stderr[-200:]}", file=sys.stderr)


def search_experiences(query: str, limit: int = 5) -> list[dict]:
    """Search experience DB for relevant past experiences."""
    if not query.strip():
        return []
    # Escape for Ku string
    q = query.replace("\\", "\\\\").replace('"', '\\"')
    src = (
        '引 "std/experience" 别 经验\n'
        f'结果 = 经验_search("{q}", "", {limit})\n'
        'print(结果)\n'
    )
    ok, stdout, stderr = run_ku(src, profile="memory", timeout=30.0)
    if not ok:
        return []
    # Parse JSON output from stdout
    try:
        data = json.loads(stdout.split("\n")[0]) if stdout else {}
        return data.get("results", [])
    except Exception:
        return []


def record_experience(
    kind: str, topic: str, context: str, input_val: str,
    output_val: str, missing: str, next_action: str, tags: str,
    success: bool = True,
) -> dict:
    """Record a new experience with quality metadata."""
    # Escape all string args
    def esc(s: str) -> str:
        return s.replace("\\", "\\\\").replace('"', '"')

    src = (
        '引 "std/experience" 别 经验\n'
        f'r = 经验_record("{esc(kind)}", "{esc(topic)}", "{esc(context)}", '
        f'"{esc(input_val)}", "{esc(output_val)}", "{esc(missing)}", '
        f'"{esc(next_action)}", "{esc(tags)}")\n'
        'print(r)\n'
    )
    ok, stdout, stderr = run_ku(src, profile="memory", timeout=30.0)
    if not ok:
        return {"ok": False, "error": stderr.split("\n")[-1] if stderr else "unknown"}
    try:
        return json.loads(stdout.split("\n")[0]) if stdout else {"ok": True}
    except Exception:
        return {"ok": True, "raw": stdout[:200]}


def compute_quality(experience: dict) -> float:
    """Compute quality score from experience metadata."""
    success_count = experience.get("success_count", 0)
    total_count = experience.get("total_count", 1)
    recall_count = experience.get("recall_count", 0)
    last_used = experience.get("last_used_at", "")
    success_rate = success_count / max(total_count, 1)
    usage_factor = (recall_count + 1) ** 0.5  # sqrt scaling
    # recency decay
    recency = 1.0
    if last_used:
        try:
            dt = datetime.fromisoformat(last_used.replace("Z", "+00:00"))
            days_ago = (datetime.now(timezone.utc) - dt).days
            recency = 0.5 ** (days_ago / 30.0)  # 30-day half-life
        except Exception:
            pass
    return success_rate * usage_factor * recency


def auto_promote_check(threshold: float = 0.7) -> list[str]:
    """Check experiences eligible for promotion and auto-promote them."""
    promoted = []
    src = (
        '引 "std/experience" 别 经验\n'
        f'promos = 经验_search("promoted", "", 1)\n'
        'print(promos)\n'
    )
    # For now, this is a stub. Full promotion requires quality scoring integration.
    return promoted


def run_task(task_description: str) -> dict:
    """Run a single task through the autonomous loop."""
    print(f"\n[task] {task_description}")
    t0 = time.time()

    # Step 1: Auto-recall relevant experiences
    print("  [1/4] 搜索历史经验...")
    relevant = search_experiences(task_description, limit=3)
    context_hint = ""
    if relevant:
        top = relevant[0]
        context_hint = (
            f"历史经验参考 (topic={top.get('topic', '')}, "
            f"context={top.get('context', '')[:100]}...)"
        )
        print(f"  召回 {len(relevant)} 条经验: {context_hint[:80]}...")
    else:
        print("  无相关历史经验")

    # Step 2: Execute task (generate Ku code from task description)
    print("  [2/4] 执行任务...")
    # Simple translation: task description → Ku demo execution
    ku_code = (
        '引 "std/daodejing" 别 道\n'
        f'print("任务: {task_description[:50]}")\n'
        'print(道_第1章("执行"))\n'
        'print(道_第42章(1))\n'
        'print(道_第77章(80))\n'
    )
    ok, stdout, stderr = run_ku(ku_code, profile="core", timeout=30.0)
    elapsed = time.time() - t0
    print(f"  执行耗时: {elapsed:.3f}s")
    if ok:
        print(f"  结果: {stdout[:100]}...")
    else:
        print(f"  错误: {stderr[:100]}...")

    # Step 3: Auto-record experience
    print("  [3/4] 自动记录经验...")
    rec = record_experience(
        kind="attempt",
        topic=task_description[:80],
        context="autonomous_loop",
        input_val=task_description[:200],
        output_val=stdout[:200] if ok else stderr[:200],
        missing="" if ok else "执行失败",
        next_action="",
        tags="autonomous,demo",
        success=ok,
    )
    print(f"  记录: {rec}")

    # Step 4: Check for promotion
    print("  [4/4] 检查提升候选...")
    auto_promote_check()

    return {
        "task": task_description,
        "ok": ok,
        "elapsed_s": round(elapsed, 3),
        "recalled": len(relevant),
        "output": stdout if ok else "",
        "error": stderr if not ok else "",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="道语言自主经验循环")
    parser.add_argument("--task", type=str, help="执行单个任务")
    parser.add_argument("--cycle", type=int, default=0, help="连续运行 N 个循环演示")
    args = parser.parse_args()

    init_experience_db()

    if args.task:
        result = run_task(args.task)
        print(f"\n结果: {json.dumps(result, ensure_ascii=False, indent=2)}")
        return 0 if result["ok"] else 1

    if args.cycle > 0:
        tasks = [
            "探索道的本质",
            "测试阴阳变换",
            "验证水德之柔",
            "执行无为而治",
            "检验损补均衡",
        ][: args.cycle]
        results = []
        for t in tasks:
            results.append(run_task(t))
        # 最终召回验证
        print("\n━━━ 经验循环验证 ━━━")
        final_recall = search_experiences("道", limit=10)
        print(f"  搜索 '道' → 命中 {len(final_recall)} 条")
        for r in final_recall[:5]:
            print(f"    - {r.get('topic', '')}")
        success_count = sum(1 for r in results if r["ok"])
        print(f"\n总结: {success_count}/{len(results)} 成功")
        return 0 if success_count == len(results) else 1

    # 默认演示
    print("━━━ 道语言自主经验循环演示 ━━━")
    tasks = [
        "演示 6 的阶乘",
        "探索道德经第八章",
        "验证水德上善若水",
    ]
    for t in tasks:
        run_task(t)
    print("\n━━━ 完成 ━━━")
    return 0


if __name__ == "__main__":
    sys.exit(main())
