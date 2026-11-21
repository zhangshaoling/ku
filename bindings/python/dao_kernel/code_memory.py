"""Agent-facing adapter for Ku's persistent code-memory CLI."""
from __future__ import annotations

import os
import re
import subprocess
import tempfile
from contextlib import contextmanager
from pathlib import Path

from . import DaoKernelError
from .thought import _find_dao_ku


class CodeMemory:
    """Save, verify, and execute Ku source as an identified code memory."""

    DEFAULT_DIRECTORY = Path(os.environ.get("KU_CODE_MEMORY_DIR", ".ku_agent_memory"))
    IDENTITY_PATTERN = re.compile(r"^ku:agent/[A-Za-z0-9._/-]+$")

    def __init__(self, directory: str | Path | None = None, module_cli: str | Path | None = None):
        self.directory = Path(directory) if directory is not None else self.DEFAULT_DIRECTORY
        self.compiler = Path(_find_dao_ku())
        if module_cli is None:
            module_cli = self.compiler.with_name("dao-module" + self.compiler.suffix)
        self.module_cli = Path(module_cli)
        if not self.module_cli.exists():
            raise DaoKernelError(f"dao-module not found: {self.module_cli}")

    @staticmethod
    def _version(version: tuple[int, int, int]) -> str:
        if len(version) != 3 or any(int(part) < 0 for part in version):
            raise ValueError("version must be a non-negative (major, minor, patch) tuple")
        return ".".join(str(int(part)) for part in version)

    @contextmanager
    def _source_file(self, source: str):
        self.directory.mkdir(parents=True, exist_ok=True)
        descriptor, name = tempfile.mkstemp(prefix=".agent-source-", suffix=".ku", dir=self.directory)
        path = Path(name)
        try:
            with os.fdopen(descriptor, "w", encoding="utf-8") as output:
                output.write(source)
            yield path
        finally:
            path.unlink(missing_ok=True)

    def save_source(self, source: str, identity: str, version: tuple[int, int, int] = (1, 0, 0),
                    *, recovery: bool = False) -> str:
        """Compile source and persist it; return the CLI confirmation text."""
        if not source or not identity:
            raise ValueError("source and identity are required")
        if not self.IDENTITY_PATTERN.fullmatch(identity):
            raise ValueError("identity must match ku:agent/<name>")
        with self._source_file(source) as temp_source:
            command = [str(self.compiler)]
            if recovery:
                command.append("--recovery")
            command += ["--store", str(self.directory), "--identity", identity,
                        self._version(version), str(temp_source)]
            result = subprocess.run(command, capture_output=True, text=True, check=False)
        if result.returncode != 0:
            raise DaoKernelError(result.stderr.strip() or result.stdout.strip() or "code memory save failed")
        return result.stdout.strip()

    def save_derived_source(self, source: str, identity: str, version: tuple[int, int, int],
                            parent_identity: str, parent_version: tuple[int, int, int],
                            *, recovery: bool = False) -> str:
        if not self.IDENTITY_PATTERN.fullmatch(identity) or not self.IDENTITY_PATTERN.fullmatch(parent_identity):
            raise ValueError("identity and parent_identity must match ku:agent/<name>")
        with self._source_file(source) as temp_source:
            command = [str(self.compiler)]
            if recovery:
                command.append("--recovery")
            command += ["--store", str(self.directory), "--identity", identity,
                        self._version(version), "--parent", parent_identity,
                        self._version(parent_version), str(temp_source)]
            result = subprocess.run(command, capture_output=True, text=True, check=False)
        if result.returncode != 0:
            raise DaoKernelError(result.stderr.strip() or result.stdout.strip() or "derived code memory save failed")
        return result.stdout.strip()

    def verify(self, identity: str, version: tuple[int, int, int] = (1, 0, 0)) -> str:
        result = subprocess.run(
            [str(self.module_cli), "verify", str(self.directory), identity, self._version(version)],
            capture_output=True, text=True, check=False,
        )
        if result.returncode != 0:
            raise DaoKernelError(result.stderr.strip() or result.stdout.strip() or "code memory verification failed")
        return result.stdout.strip()

    def run(self, identity: str, export: str = "main",
            version: tuple[int, int, int] = (1, 0, 0)) -> int:
        result = subprocess.run(
            [str(self.module_cli), "run", str(self.directory), identity,
             self._version(version), export], capture_output=True, text=True, check=False,
        )
        if result.returncode != 0:
            raise DaoKernelError(result.stderr.strip() or result.stdout.strip() or "code memory execution failed")
        try:
            return int(result.stdout.strip())
        except ValueError as exc:
            raise DaoKernelError(f"invalid code memory result: {result.stdout!r}") from exc
