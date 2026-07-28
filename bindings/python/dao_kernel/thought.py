"""Thought — the fundamental executable unit on the new Ku kernel.

Each thought is:
- Code: compiled Ku module bytes (the .dao binary)
- Memory: the same bytes, persistable to disk
- Structure: name, params, doc, metadata
- Behavior: callable via the C ABI

This bridges the old chain Thought interface to the new kernel.
"""

from __future__ import annotations

import re
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Any, Optional

from . import DaoKernelError, DaoValue, DAO_VALUE_I64, Runtime


# FNV-1a hash used by the Ku migration compiler for export symbol_ids
def fnv1a(name: str) -> int:
    h = 2166136261
    for c in name.encode():
        h ^= c
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def _find_kernel_library() -> Path:
    """Locate the compiled kernel DLL/SO."""
    candidates = [
        Path(__file__).resolve().parent.parent.parent.parent
            / "kernel" / "out" / "cmake" / "bin" / "libdao_kernel.dll",
        Path(__file__).resolve().parent.parent.parent.parent
            / "kernel" / "out" / "cmake" / "bin" / "dao_kernel.dll",
        Path(__file__).resolve().parent.parent.parent.parent
            / "kernel" / "out" / "cmake" / "libdao_kernel.dll",
    ]
    for path in candidates:
        if path.exists():
            return path
    raise DaoKernelError(
        "kernel library not found. Run: .\\tools\\build_kernel.ps1"
    )


def _find_dao_ku() -> Path:
    """Locate the dao-ku compiler executable."""
    candidates = [
        Path(__file__).resolve().parent.parent.parent.parent
            / "kernel" / "out" / "cmake" / "bin" / "dao-ku.exe",
        Path(__file__).resolve().parent.parent.parent.parent
            / "kernel" / "out" / "cmake" / "bin" / "dao-ku",
    ]
    for path in candidates:
        if path.exists():
            return path
    raise DaoKernelError(
        "dao-ku compiler not found. Run: .\\tools\\build_kernel.ps1"
    )


class Thought:
    """A Ku Thought backed by the new kernel.

    Wraps a compiled module and provides the classic Thought interface:
    - call(runtime, args): execute with integer arguments
    - to_bytes(): serialize to module bytes (persistent memory)
    - metadata: name, params, doc, executions, version

    Convention: the .ku source must define `thought main(...)` as the
    entry point. The Thought`s name is its registry identity.
    """

    registry: dict[str, "Thought"] = {}
    _default_runtime: Optional[Runtime] = None

    def __init__(
        self,
        name: str,
        params: list[str],
        module_bytes: bytes,
        doc: str = "",
        meta: Optional[dict[str, Any]] = None,
    ) -> None:
        self.name = name
        self.params = params
        self.module_bytes = module_bytes
        self.doc = doc
        self.meta: dict[str, Any] = meta or {
            "created": time.time(),
            "executions": 0,
            "version": 1,
        }
        Thought.registry[name] = self

    @classmethod
    def default_runtime(cls) -> Runtime:
        """Lazily create a shared Runtime for all Thoughts."""
        if cls._default_runtime is None:
            cls._default_runtime = Runtime(_find_kernel_library())
        return cls._default_runtime

    @classmethod
    def from_source(
        cls,
        name: str,
        source: str,
        doc: str = "",
        params: Optional[list[str]] = None,
    ) -> "Thought":
        """Compile a Thought from .ku source."""
        dao_ku = _find_dao_ku()

        if params is None:
            params = cls._infer_params(source)

        with tempfile.NamedTemporaryFile(
            suffix=".ku", mode="w", delete=False, encoding="utf-8"
        ) as src:
            src.write(source)
            src_path = Path(src.name)

        with tempfile.NamedTemporaryFile(
            suffix=".dao", delete=False
        ) as out:
            out_path = Path(out.name)

        try:
            result = subprocess.run(
                [str(dao_ku), str(src_path), str(out_path)],
                capture_output=True, text=True,
            )
            if result.returncode != 0:
                raise DaoKernelError(
                    f"compilation failed: {result.stderr.strip()}"
                )
            module_bytes = out_path.read_bytes()
        finally:
            src_path.unlink(missing_ok=True)
            out_path.unlink(missing_ok=True)

        return cls(name, params, module_bytes, doc)

    @staticmethod
    def _infer_params(source: str) -> list[str]:
        """Infer parameter names from .ku source."""
        match = re.search(r"thought\s+main\s*\(([^)]*)\)", source)
        if not match:
            return []
        return [p.strip() for p in match.group(1).split(",") if p.strip()]

    def call(
        self,
        runtime: Optional[Runtime] = None,
        args: Optional[list[int]] = None,
    ) -> DaoValue:
        """Execute this thought with the given integer args."""
        runtime = runtime or self.default_runtime()
        args = args or []
        self.meta["executions"] = self.meta.get("executions", 0) + 1
        module = runtime.load(self.module_bytes)
        try:
            return module.call(fnv1a("main"), *args)
        finally:
            module.close()

    def call_i64(
        self,
        runtime: Optional[Runtime] = None,
        args: Optional[list[int]] = None,
    ) -> int:
        """Execute and return i64 result."""
        result = self.call(runtime, args)
        if result.type != DAO_VALUE_I64:
            raise DaoKernelError(
                f"expected i64 result, got type {result.type}"
            )
        return result.payload

    def to_bytes(self) -> bytes:
        """Serialize to module bytes (persistent memory)."""
        return self.module_bytes

    @classmethod
    def from_bytes(
        cls,
        name: str,
        data: bytes,
        doc: str = "",
        params: Optional[list[str]] = None,
    ) -> "Thought":
        """Deserialize from module bytes."""
        return cls(name, params or [], data, doc)

    def __repr__(self) -> str:
        return (
            f"Thought({self.name}, params={self.params}, "
            f"execs={self.meta.get('executions', 0)})"
        )