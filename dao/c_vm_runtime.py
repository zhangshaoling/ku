"""C VM runtime gateway for Dao/Ku.

This module is the single Python orchestration boundary for executing Ku
through the native C VM. It intentionally does not call Python Thought.call,
DaoVM, compile_道, or run_bytecode in the main execution path.
"""

from __future__ import annotations

import json
import os
import subprocess
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping, Sequence


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BINARY = ROOT / "dao" / "dao_core.exe"
DEFAULT_BOOTSTRAP = ROOT / "demos" / "frontend_bootstrap.kub.json"
STD_DIR = ROOT / "dao" / "std"

PROFILES: dict[str, tuple[Path, ...]] = {
    "frontend": (),
    "core": (
        STD_DIR / "math.ku",
        STD_DIR / "string.ku",
        STD_DIR / "list.ku",
        STD_DIR / "type.ku",
    ),
    # task_queue must load before experience: experience.gap_to_task calls submit().
    "memory": (
        STD_DIR / "math.ku",
        STD_DIR / "string.ku",
        STD_DIR / "list.ku",
        STD_DIR / "type.ku",
        STD_DIR / "task_queue.ku",
        STD_DIR / "experience.ku",
    ),
    "tiandao_mcp": (
        STD_DIR / "tiandao_mcp.ku",
    ),
    "tiandao_fast": (
        STD_DIR / "math.ku",
        STD_DIR / "string.ku",
        STD_DIR / "list.ku",
        STD_DIR / "type.ku",
        STD_DIR / "memory.ku",
        STD_DIR / "tool.ku",
        STD_DIR / "tiandao_fast.ku",
    ),
    "tiandao": (
        STD_DIR / "math.ku",
        STD_DIR / "string.ku",
        STD_DIR / "list.ku",
        STD_DIR / "type.ku",
        STD_DIR / "memory.ku",
        STD_DIR / "tool.ku",
        STD_DIR / "daodejing.ku",
        STD_DIR / "tiandao.ku",
    ),
    "semantic": (
        STD_DIR / "math.ku",
        STD_DIR / "string.ku",
        STD_DIR / "list.ku",
        STD_DIR / "type.ku",
        STD_DIR / "memory_env.ku",
        STD_DIR / "trace.ku",
        STD_DIR / "semantic_core.ku",
        STD_DIR / "patch.ku",
    ),
}


@dataclass(frozen=True)
class DaoResult:
    ok: bool
    value: Any = None
    stdout: str = ""
    stderr: str = ""
    returncode: int = 0
    error: str | None = None
    command: tuple[str, ...] = ()
    elapsed_ms: float = 0.0


class CVMRuntime:
    def __init__(
        self,
        root: Path | None = None,
        binary: Path | None = None,
        bootstrap: Path | None = None,
        data_dir: Path | str | None = None,
        timeout: float = 60.0,
    ):
        self.root = Path(root) if root is not None else ROOT
        self.binary = Path(binary) if binary is not None else DEFAULT_BINARY
        self.bootstrap = Path(bootstrap) if bootstrap is not None else DEFAULT_BOOTSTRAP
        self.data_dir = Path(data_dir) if data_dir is not None else None
        self.timeout = timeout

    def profile_modules(self, profile: str) -> tuple[Path, ...]:
        try:
            return PROFILES[profile]
        except KeyError as exc:
            raise ValueError(f"unknown C VM profile: {profile}") from exc

    def run_source(
        self,
        source: str,
        *,
        profile: str = "core",
        extra_modules: Sequence[Path | str] = (),
        data_dir: Path | str | None = None,
        timeout: float | None = None,
        env: Mapping[str, str] | None = None,
    ) -> DaoResult:
        if not self.binary.exists():
            return DaoResult(
                ok=False,
                error=f"C VM binary not found: {self.binary}",
                returncode=127,
                command=(str(self.binary),),
            )
        if not self.bootstrap.exists():
            return DaoResult(
                ok=False,
                error=f"C VM bootstrap not found: {self.bootstrap}",
                returncode=127,
                command=(str(self.binary), "--bootstrap", str(self.bootstrap)),
            )

        modules = [*self.profile_modules(profile), *(Path(p) for p in extra_modules)]
        proc_env = os.environ.copy()
        if env:
            proc_env.update(env)
        effective_data_dir = data_dir if data_dir is not None else self.data_dir
        if effective_data_dir is not None:
            proc_env["DAO_DATA_DIR"] = str(effective_data_dir)

        with tempfile.TemporaryDirectory(prefix="dao_cvm_runtime_") as raw_tmpdir:
            tmpdir = Path(raw_tmpdir)
            source_path = tmpdir / "program.ku"
            source_path.write_text(source, encoding="utf-8")
            cmd = [
                str(self.binary),
                "--bootstrap",
                str(self.bootstrap),
                *(str(p) for p in modules),
                str(source_path),
            ]
            start = time.perf_counter()
            try:
                proc = subprocess.run(
                    cmd,
                    cwd=self.root,
                    env=proc_env,
                    capture_output=True,
                    text=True,
                    encoding="utf-8",
                    errors="replace",
                    timeout=timeout if timeout is not None else self.timeout,
                )
            except subprocess.TimeoutExpired as exc:
                elapsed_ms = (time.perf_counter() - start) * 1000
                return DaoResult(
                    ok=False,
                    stdout=exc.stdout or "",
                    stderr=exc.stderr or "",
                    returncode=-1,
                    error=f"C VM execution timed out after {exc.timeout}s",
                    command=tuple(cmd),
                    elapsed_ms=elapsed_ms,
                )
            elapsed_ms = (time.perf_counter() - start) * 1000

        stdout = proc.stdout.strip()
        stderr = proc.stderr.strip()
        if proc.returncode != 0:
            return DaoResult(
                ok=False,
                stdout=stdout,
                stderr=stderr,
                returncode=proc.returncode,
                error=stderr or stdout or f"C VM exited with {proc.returncode}",
                command=tuple(cmd),
                elapsed_ms=elapsed_ms,
            )

        value = parse_cvm_stdout(stdout)
        if isinstance(value, str) and _looks_like_runtime_error(value):
            return DaoResult(
                ok=False,
                value=value,
                stdout=stdout,
                stderr=stderr,
                returncode=0,
                error=value,
                command=tuple(cmd),
                elapsed_ms=elapsed_ms,
            )
        return DaoResult(
            ok=True,
            value=value,
            stdout=stdout,
            stderr=stderr,
            returncode=0,
            command=tuple(cmd),
            elapsed_ms=elapsed_ms,
        )

    def eval_code(
        self,
        code: str,
        *,
        profile: str = "core",
        data_dir: Path | str | None = None,
        timeout: float | None = None,
    ) -> DaoResult:
        return self.run_source(code, profile=profile, data_dir=data_dir, timeout=timeout)

    def call_thought(
        self,
        name: str,
        args: Mapping[str, Any] | Sequence[Any] | None = None,
        *,
        params: Sequence[str] | None = None,
        profile: str = "core",
        data_dir: Path | str | None = None,
        timeout: float | None = None,
    ) -> DaoResult:
        ordered_args = normalize_args(args, params)
        args_json = json.dumps(ordered_args, ensure_ascii=False)
        escaped = json.dumps(args_json, ensure_ascii=False)
        call_args = ", ".join(f"__dao_args[{i}]" for i in range(len(ordered_args)))
        source = f"__dao_args = json_parse({escaped})\n{name}({call_args})\n"
        return self.run_source(source, profile=profile, data_dir=data_dir, timeout=timeout)

    def list_thoughts(self, *, profile: str = "core", timeout: float | None = None) -> DaoResult:
        return self.run_source("list_thoughts()\n", profile=profile, timeout=timeout)


def normalize_args(args: Mapping[str, Any] | Sequence[Any] | None, params: Sequence[str] | None) -> list[Any]:
    if args is None:
        return []
    if isinstance(args, Mapping):
        if params is None:
            raise ValueError("params are required when call_thought args are a mapping")
        return [args.get(p) for p in params]
    if isinstance(args, (str, bytes)):
        raise TypeError("call_thought args must be a sequence of values, not a string")
    return list(args)


def parse_cvm_stdout(stdout: str) -> Any:
    text = stdout.strip()
    if text == "":
        return None
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return text


def _looks_like_runtime_error(value: str) -> bool:
    return (
        value.startswith("NameError:")
        or value.startswith("RuntimeError:")
        or value.startswith("Error:")
        or value.startswith("sqlite error:")
    )
