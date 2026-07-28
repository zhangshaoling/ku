"""Build the legacy Dao C VM with an available cross-platform C compiler."""

from __future__ import annotations

import argparse
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "dao" / "dao_core.c"
SQLITE_SOURCE = ROOT / "vendor" / "sqlite3.c"
SQLITE_HEADER = ROOT / "vendor" / "sqlite3.h"
DEFAULT_OUTPUT = ROOT / "dao" / ("dao_core.exe" if os.name == "nt" else "dao_core")


def compiler_command() -> list[str]:
    configured = os.environ.get("CC")
    if configured:
        return shlex.split(configured)
    for name in ("cc", "clang", "gcc"):
        found = shutil.which(name)
        if found:
            return [found]
    if os.name == "nt":
        for candidate in (
            Path("C:/msys64/ucrt64/bin/gcc.exe"),
            Path("C:/msys64/mingw64/bin/gcc.exe"),
            Path("C:/mingw64/bin/gcc.exe"),
        ):
            if candidate.exists():
                return [str(candidate)]
    managed = ROOT / ".toolchain" / "llvm-mingw" / "bin" / "clang.exe"
    if os.name == "nt" and managed.exists():
        return [str(managed)]
    raise RuntimeError("No C compiler found; set CC or install clang/gcc")


def build(output: Path) -> None:
    inputs = (SOURCE, SQLITE_SOURCE, SQLITE_HEADER, Path(__file__))
    if output.exists() and output.stat().st_mtime_ns >= max(
        path.stat().st_mtime_ns for path in inputs
    ):
        print(f"up to date {output}")
        return
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [
        *compiler_command(), "-DSQLITE_ENABLE_FTS5", "-O2", "-Wall",
        "-o", str(output), str(SOURCE), str(SQLITE_SOURCE), "-lm",
    ]
    if os.name == "nt":
        command.append("-lws2_32")
    elif sys.platform.startswith("linux"):
        command.extend(["-ldl", "-pthread"])
    else:
        command.append("-pthread")
    subprocess.run(command, cwd=ROOT, check=True)
    print(f"built {output} with {command[0]}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    try:
        build(args.output.resolve())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"build failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
