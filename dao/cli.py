from __future__ import annotations

import argparse
import json
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

    args = parser.parse_args(argv)
    if not hasattr(args, "func"):
        parser.print_help()
        return 2
    return args.func(args)
