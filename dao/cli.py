from __future__ import annotations

import argparse
import json
import os
import sqlite3
import sys
from pathlib import Path

from .c_vm_runtime import CVMRuntime, PROFILES


def _print_value(value):
    if isinstance(value, (dict, list)):
        print(json.dumps(value, ensure_ascii=False))
    elif value is None:
        print("null")
    else:
        print(value)


def _add_common_runtime_args(parser):
    parser.add_argument(
        "--profile",
        default="core",
        choices=sorted(PROFILES.keys()),
        help="C VM profile to load before running code",
    )
    parser.add_argument(
        "--data-dir",
        default=None,
        help="DAO_DATA_DIR for SQLite-backed memory and runtime data",
    )
    parser.add_argument(
        "--module",
        action="append",
        default=[],
        help="Additional .ku module to load before the program; can be repeated",
    )


def run_file(args):
    path = Path(args.file)
    source = path.read_text(encoding="utf-8")
    runtime = CVMRuntime(timeout=args.timeout)
    result = runtime.run_source(
        source,
        profile=args.profile,
        extra_modules=[Path(item) for item in args.module],
        data_dir=args.data_dir,
    )
    if not result.ok:
        message = result.error or result.stderr or result.stdout or "C VM execution failed"
        print(message, file=sys.stderr)
        return result.returncode or 1
    _print_value(result.value)
    return 0


def repl(args):
    runtime = CVMRuntime(timeout=args.timeout)
    print("Dao REPL. Type :quit or :q to exit. Each line runs in a fresh C VM frame.")
    while True:
        try:
            line = input("dao> ")
        except EOFError:
            print()
            return 0
        code = line.strip()
        if not code:
            continue
        if code in {":q", ":quit", "quit", "exit"}:
            return 0
        result = runtime.run_source(
            code,
            profile=args.profile,
            extra_modules=[Path(item) for item in args.module],
            data_dir=args.data_dir,
        )
        if not result.ok:
            print(result.error or result.stderr or result.stdout or "C VM execution failed", file=sys.stderr)
            continue
        _print_value(result.value)


def status(_):
    data_dir = os.environ.get("DAO_DATA_DIR", ".")
    db_path = os.path.join(data_dir, "memory.db")
    if not Path(db_path).exists():
        print(f"[status] memory.db not found at {db_path}")
        return 1
    try:
        conn = sqlite3.connect(db_path)
        conn.row_factory = sqlite3.Row
        exp_cnt = conn.execute("SELECT COUNT(*) FROM experience").fetchone()[0]
        exp_active = conn.execute("SELECT COUNT(*) FROM experience WHERE quality_status='active'").fetchone()[0]
        kw_cnt = conn.execute("SELECT COUNT(*) FROM knowledge").fetchone()[0]
        tool_cnt = conn.execute("SELECT COUNT(*) FROM tool_registry").fetchone()[0]
        tool_active = conn.execute("SELECT COUNT(*) FROM tool_registry WHERE status='active' AND quality_status='active'").fetchone()[0]
        tool_promotable = conn.execute("SELECT COUNT(*) FROM tool_registry WHERE trust >= 0.75 AND call_count >= 3").fetchone()[0]
        cpt_cnt = conn.execute("SELECT COUNT(*) FROM concept").fetchone()[0]
        goal_cnt = conn.execute("SELECT COUNT(*) FROM goal WHERE status='active'").fetchone()[0]
        rel_cnt = conn.execute("SELECT COUNT(*) FROM relation").fetchone()[0]
        conn.close()
    except Exception as e:
        print(f"[status] db error: {e}")
        return 1

    # ── 终端简易 bar 图表 ──
    def bar(label, value, max_val=256, width=16):
        filled = int(min(value, max_val) / max_val * width)
        bar_str = "█" * filled + "░" * (width - filled)
        return f" {label} {bar_str} {value}"

    print("┌─────────────────────────────────────┐")
    print("│         天 道 统 计 面 板          │")
    print("├─────────────────────────────────────┤")
    print(f"│ 经验 {bar('active', exp_active, exp_cnt if exp_cnt > 0 else 1).ljust(34)} / {exp_cnt:>4} total │")
    print(f"│ 知识 {bar('total', kw_cnt).ljust(34)}          │")
    print(f"│ 工具 {bar('active', tool_active, tool_cnt if tool_cnt > 0 else 1).ljust(34)} ({tool_promotable:>2} promotable) │")
    print(f"│ 概念 {bar('total', cpt_cnt).ljust(34)}          │")
    print(f"│ 目标 {bar('active', goal_cnt).ljust(34)}          │")
    print(f"│ 关系 {bar('edges', rel_cnt).ljust(34)}          │")
    print("└─────────────────────────────────────┘")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(prog="dao", description="Dao C VM command line tools")
    sub = parser.add_subparsers(dest="command")

    run_parser = sub.add_parser("run", help="Run a .ku source file through dao_core.exe")
    _add_common_runtime_args(run_parser)
    run_parser.add_argument("--timeout", type=float, default=60.0, help="Execution timeout in seconds")
    run_parser.add_argument("file", help=".ku file to run")
    run_parser.set_defaults(func=run_file)

    repl_parser = sub.add_parser("repl", help="Start a minimal Dao REPL backed by dao_core.exe")
    _add_common_runtime_args(repl_parser)
    repl_parser.add_argument("--timeout", type=float, default=60.0, help="Execution timeout in seconds")
    repl_parser.set_defaults(func=repl)

    status_parser = sub.add_parser("status", help="显示天道统计面板：经验/知识/工具/概念/目标/关系")
    status_parser.set_defaults(func=status)

    args = parser.parse_args(argv)
    if not hasattr(args, "func"):
        parser.print_help()
        return 2
    return args.func(args)
