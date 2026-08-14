"""C VM runtime gateway for Dao/Ku.

This module is the single Python orchestration boundary for executing Ku
through the native C VM. It intentionally does not call Python Thought.call,
DaoVM, compile_道, or run_bytecode in the main execution path.
"""

from __future__ import annotations

import hashlib
import json
import os
import queue
import signal
import subprocess
import sys
import threading
import time
from collections import deque
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from typing import Any, Mapping, Sequence


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BINARY = ROOT / "dao" / ("dao_core.exe" if os.name == "nt" else "dao_core")
DEFAULT_BOOTSTRAP = ROOT / "demos" / "frontend_bootstrap.kub.json"
STD_DIR = ROOT / "dao" / "std"
SNAPSHOT_DIR = STD_DIR / "snapshots"
SNAPSHOT_MANIFEST = SNAPSHOT_DIR / "manifest.json"
SNAPSHOT_FORMAT_VERSION = 1
VM_ABI = "kub-0.1"

PROFILES: dict[str, tuple[Path, ...]] = {
    "frontend": (),
    "core": (
        STD_DIR / "math.ku",
        STD_DIR / "string.ku",
        STD_DIR / "list.ku",
        STD_DIR / "type.ku",
    ),
    "memory": (
        STD_DIR / "math.ku",
        STD_DIR / "string.ku",
        STD_DIR / "list.ku",
        STD_DIR / "type.ku",
        STD_DIR / "experience.ku",
        STD_DIR / "memory_graph.ku",
    ),
    # task_queue must load before experience: experience.gap_to_task calls submit().
    "memory_tasks": (
        STD_DIR / "math.ku",
        STD_DIR / "string.ku",
        STD_DIR / "list.ku",
        STD_DIR / "type.ku",
        STD_DIR / "task_queue.ku",
        STD_DIR / "experience.ku",
        STD_DIR / "memory_graph.ku",
    ),
    "memory_lifecycle": (
        STD_DIR / "math.ku",
        STD_DIR / "string.ku",
        STD_DIR / "list.ku",
        STD_DIR / "type.ku",
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


@lru_cache(maxsize=1)
def _snapshot_manifest() -> dict[str, Any]:
    try:
        value = json.loads(SNAPSHOT_MANIFEST.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    return value if isinstance(value, dict) else {}


@lru_cache(maxsize=64)
def _snapshot_identity(path: str, modified_ns: int, size: int) -> tuple[str, dict[str, Any]]:
    del modified_ns, size
    try:
        raw = Path(path).read_bytes()
        value = json.loads(raw)
    except (OSError, json.JSONDecodeError):
        return "", {}
    return hashlib.sha256(raw).hexdigest(), value if isinstance(value, dict) else {}


def _module_snapshot(source: Path, bootstrap: Path) -> Path:
    manifest = _snapshot_manifest()
    if (
        manifest.get("format_version") != SNAPSHOT_FORMAT_VERSION
        or manifest.get("vm_abi") != VM_ABI
    ):
        return source
    modules = manifest.get("modules")
    entry = modules.get(source.name) if isinstance(modules, dict) else None
    if not isinstance(entry, dict):
        return source
    snapshot_name = entry.get("snapshot")
    expected_hash = entry.get("source_sha256")
    if not isinstance(snapshot_name, str) or not isinstance(expected_hash, str):
        return source
    try:
        actual_hash = hashlib.sha256(source.read_bytes()).hexdigest()
        bootstrap_hash = hashlib.sha256(bootstrap.read_bytes()).hexdigest()
    except OSError:
        return source
    snapshot = SNAPSHOT_DIR / snapshot_name
    try:
        snapshot_stat = snapshot.stat()
    except OSError:
        return source
    snapshot_hash, metadata = _snapshot_identity(
        str(snapshot), snapshot_stat.st_mtime_ns, snapshot_stat.st_size,
    )
    valid = (
        actual_hash == expected_hash == metadata.get("source_sha256")
        and bootstrap_hash == manifest.get("bootstrap_sha256")
        == metadata.get("bootstrap_sha256")
        and snapshot_hash == entry.get("snapshot_sha256")
        and metadata.get("format") == "kub"
        and metadata.get("version") == "0.1"
        and metadata.get("format_version") == SNAPSHOT_FORMAT_VERSION
        and metadata.get("vm_abi") == VM_ABI
        and metadata.get("module_id") == entry.get("module_id")
        and metadata.get("exports") == entry.get("exports")
    )
    return snapshot if valid else source


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


def _process_private_memory_mb(process: subprocess.Popen) -> float | None:
    if process.poll() is not None:
        return None
    if sys.platform.startswith("linux"):
        try:
            private_kb = 0
            with open(f"/proc/{process.pid}/smaps_rollup", encoding="ascii") as handle:
                for line in handle:
                    name, _, value = line.partition(":")
                    if name in {"Private_Clean", "Private_Dirty", "Private_Hugetlb"}:
                        private_kb += int(value.split()[0])
            if private_kb:
                return private_kb / 1024
        except (OSError, ValueError, IndexError):
            pass
        try:
            with open(f"/proc/{process.pid}/status", encoding="ascii") as handle:
                for line in handle:
                    if line.startswith("VmRSS:"):
                        return int(line.split()[1]) / 1024
        except (OSError, ValueError, IndexError):
            return None
        return None
    if sys.platform == "darwin":
        try:
            import ctypes

            buffer = ctypes.create_string_buffer(256)
            libproc = ctypes.CDLL("/usr/lib/libproc.dylib")
            if libproc.proc_pid_rusage(process.pid, 4, ctypes.byref(buffer)) == 0:
                footprint = ctypes.c_uint64.from_buffer(buffer, 72).value
                if footprint:
                    return footprint / (1024 * 1024)
        except (AttributeError, OSError, ValueError):
            pass
    if os.name != "nt":
        try:
            measured = subprocess.run(
                ["ps", "-o", "rss=", "-p", str(process.pid)],
                capture_output=True, text=True, timeout=0.5, check=False,
            )
            if measured.returncode == 0 and measured.stdout.strip():
                return int(measured.stdout.strip().split()[0]) / 1024
        except (OSError, ValueError, subprocess.TimeoutExpired):
            return None
        return None
    try:
        import ctypes
        from ctypes import wintypes

        class PROCESS_MEMORY_COUNTERS(ctypes.Structure):
            _fields_ = [
                ("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD),
                ("PeakWorkingSetSize", ctypes.c_size_t),
                ("WorkingSetSize", ctypes.c_size_t),
                ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                ("PagefileUsage", ctypes.c_size_t),
                ("PeakPagefileUsage", ctypes.c_size_t),
            ]

        counters = PROCESS_MEMORY_COUNTERS()
        counters.cb = ctypes.sizeof(counters)
        handle = wintypes.HANDLE(int(process._handle))
        ok = ctypes.windll.psapi.GetProcessMemoryInfo(
            handle, ctypes.byref(counters), counters.cb)
        return counters.PagefileUsage / (1024 * 1024) if ok else None
    except (AttributeError, OSError, ValueError):
        return None


def _terminate_process_tree(process: subprocess.Popen) -> None:
    if process.poll() is not None:
        return
    try:
        if os.name == "nt":
            subprocess.run(
                ["taskkill", "/PID", str(process.pid), "/T", "/F"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=3,
                check=False,
                creationflags=0x08000000,  # CREATE_NO_WINDOW
            )
        else:
            os.killpg(process.pid, signal.SIGKILL)
    except (OSError, subprocess.TimeoutExpired):
        try:
            process.kill()
        except OSError:
            pass
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        try:
            process.kill()
        except OSError:
            pass


class _CVMWorker:
    def __init__(self, command: Sequence[str], env: Mapping[str, str], cwd: Path,
                 startup_timeout: float, max_requests: int, max_private_mb: float) -> None:
        creationflags = 0x00004200 if os.name == "nt" else 0
        self.command = tuple(command)
        self.max_requests = max_requests
        self.max_private_mb = max_private_mb
        self.request_count = 0
        self.active_calls = 0
        self._lock = threading.Lock()
        self._responses: queue.Queue[str | None] = queue.Queue(maxsize=4)
        self._stderr: deque[str] = deque(maxlen=64)
        self._protocol_error: str | None = None
        self.last_used = time.monotonic()
        self._next_request_id = 1
        self._process = subprocess.Popen(
            command,
            cwd=cwd,
            env=dict(env),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
            creationflags=creationflags,
            start_new_session=os.name != "nt",
        )
        self._stdout_thread = threading.Thread(target=self._read_stdout, daemon=True)
        self._stderr_thread = threading.Thread(target=self._read_stderr, daemon=True)
        self._stdout_thread.start()
        self._stderr_thread.start()
        deadline = time.monotonic() + startup_timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                self.close()
                raise TimeoutError(
                    f"C VM worker startup timed out after {startup_timeout}s")
            try:
                ready = self._responses.get(timeout=min(0.1, remaining))
                break
            except queue.Empty:
                private_mb = self.private_memory_mb()
                if private_mb is not None and private_mb >= self.max_private_mb:
                    self.close()
                    raise RuntimeError(
                        f"C VM worker exceeded private memory limit "
                        f"({private_mb:.1f} MB >= {self.max_private_mb:.1f} MB) during startup"
                    )
        if ready is None:
            self.close()
            raise RuntimeError("C VM worker exited during startup")
        try:
            ready_value = json.loads(ready)
        except json.JSONDecodeError as exc:
            self.close()
            raise RuntimeError(f"invalid C VM worker greeting: {ready}") from exc
        if ready_value != {"ready": True}:
            self.close()
            raise RuntimeError(f"unexpected C VM worker greeting: {ready_value}")
        startup_private_mb = self.private_memory_mb()
        if startup_private_mb is None:
            self.close()
            raise RuntimeError(
                "C VM worker memory measurement is unavailable on this platform"
            )
        if startup_private_mb >= self.max_private_mb:
            self.close()
            raise RuntimeError(
                f"C VM worker exceeded private memory limit "
                f"({startup_private_mb:.1f} MB >= {self.max_private_mb:.1f} MB) "
                "during startup"
            )

    def _read_stdout(self) -> None:
        assert self._process.stdout is not None
        for line in self._process.stdout:
            try:
                self._responses.put_nowait(line.rstrip("\r\n"))
            except queue.Full:
                self._protocol_error = "C VM worker response queue overflow"
                _terminate_process_tree(self._process)
                break
        try:
            self._responses.put_nowait(None)
        except queue.Full:
            pass

    def _read_stderr(self) -> None:
        assert self._process.stderr is not None
        for line in self._process.stderr:
            self._stderr.append(line.rstrip("\r\n"))

    @property
    def stderr(self) -> str:
        return "\n".join(self._stderr)

    @property
    def reusable(self) -> bool:
        return (
            self._protocol_error is None
            and self._process.poll() is None
            and self.request_count < self.max_requests
        )

    def call(self, source: str, timeout: float) -> str:
        with self._lock:
            if not self.reusable:
                raise RuntimeError("C VM worker is not reusable")
            assert self._process.stdin is not None
            request_id = self._next_request_id
            self._next_request_id += 1
            try:
                request = {"id": request_id, "source": source}
                self._process.stdin.write(json.dumps(request, ensure_ascii=False) + "\n")
                self._process.stdin.flush()
            except (BrokenPipeError, OSError) as exc:
                raise RuntimeError("C VM worker pipe closed") from exc
            deadline = time.monotonic() + timeout
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    self.close()
                    raise TimeoutError(f"C VM execution timed out after {timeout}s")
                try:
                    response = self._responses.get(timeout=min(0.1, remaining))
                    break
                except queue.Empty:
                    private_mb = self.private_memory_mb()
                    if private_mb is not None and private_mb >= self.max_private_mb:
                        self.close()
                        raise RuntimeError(
                            f"C VM worker exceeded private memory limit "
                            f"({private_mb:.1f} MB >= {self.max_private_mb:.1f} MB)"
                        )
            if response is None:
                raise RuntimeError(
                    self._protocol_error or self.stderr or "C VM worker exited")
            try:
                envelope = json.loads(response)
            except json.JSONDecodeError as exc:
                raise RuntimeError("invalid C VM worker response") from exc
            if not isinstance(envelope, dict) or envelope.get("id") != request_id:
                raise RuntimeError("C VM worker response id mismatch")
            self.request_count += 1
            self.last_used = time.monotonic()
            return response

    def close(self) -> None:
        stdin = self._process.stdin
        stdout = self._process.stdout
        stderr = self._process.stderr
        if self._process.poll() is None and stdin is not None:
            try:
                stdin.close()
            except OSError:
                pass
        if self._process.poll() is None:
            try:
                self._process.wait(timeout=0.5)
            except subprocess.TimeoutExpired:
                _terminate_process_tree(self._process)
        for stream in (stdout, stderr, stdin):
            if stream is not None and not stream.closed:
                try:
                    stream.close()
                except OSError:
                    pass
        self._stdout_thread.join(timeout=1)
        self._stderr_thread.join(timeout=1)

    def private_memory_mb(self) -> float | None:
        return _process_private_memory_mb(self._process)


class CVMRuntime:
    def __init__(
        self,
        root: Path | None = None,
        binary: Path | None = None,
        bootstrap: Path | None = None,
        data_dir: Path | str | None = None,
        timeout: float = 60.0,
        persistent: bool = False,
        max_worker_requests: int | None = None,
        max_workers: int | None = None,
        max_private_mb: float | None = None,
        max_worker_idle_seconds: float | None = None,
    ):
        self.root = Path(root) if root is not None else ROOT
        configured_binary = binary or os.environ.get("DAO_CVM_BINARY")
        self.binary = Path(configured_binary) if configured_binary else DEFAULT_BINARY
        self.bootstrap = Path(bootstrap) if bootstrap is not None else DEFAULT_BOOTSTRAP
        self.data_dir = Path(data_dir) if data_dir is not None else None
        self.timeout = timeout
        self.persistent = persistent
        self.max_worker_requests = max_worker_requests or int(
            os.environ.get("DAO_CVM_MAX_REQUESTS", "32"))
        self.max_workers = max_workers or int(os.environ.get("DAO_CVM_MAX_WORKERS", "2"))
        self.max_private_mb = max_private_mb or float(
            os.environ.get("DAO_CVM_MAX_PRIVATE_MB", "256"))
        self.max_worker_idle_seconds = max_worker_idle_seconds or float(
            os.environ.get("DAO_CVM_IDLE_SECONDS", "300"))
        self._workers: dict[
            tuple[str, tuple[str, ...], str, tuple[tuple[str, str], ...]], _CVMWorker
        ] = {}
        self._workers_lock = threading.Condition()

    def profile_modules(self, profile: str) -> tuple[Path, ...]:
        try:
            return tuple(
                _module_snapshot(source, self.bootstrap) for source in PROFILES[profile]
            )
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

        effective_timeout = timeout if timeout is not None else self.timeout
        if self.persistent:
            return self._run_worker(
                source, profile, modules, proc_env, effective_data_dir, effective_timeout,
                env or {})

        import tempfile
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
            creationflags = 0x00004200 if os.name == "nt" else 0
            proc = subprocess.Popen(
                cmd,
                cwd=self.root,
                env=proc_env,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                creationflags=creationflags,
                start_new_session=os.name != "nt",
            )
            deadline = time.monotonic() + effective_timeout
            failure: str | None = None
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    failure = f"C VM execution timed out after {effective_timeout}s"
                    break
                try:
                    stdout, stderr = proc.communicate(timeout=min(0.1, remaining))
                    break
                except subprocess.TimeoutExpired:
                    private_mb = _process_private_memory_mb(proc)
                    if private_mb is not None and private_mb >= self.max_private_mb:
                        failure = (
                            f"C VM exceeded private memory limit "
                            f"({private_mb:.1f} MB >= {self.max_private_mb:.1f} MB)"
                        )
                        break
            if failure is not None:
                _terminate_process_tree(proc)
                stdout, stderr = proc.communicate()
                elapsed_ms = (time.perf_counter() - start) * 1000
                return DaoResult(
                    ok=False,
                    stdout=stdout or "",
                    stderr=stderr or "",
                    returncode=-1,
                    error=failure,
                    command=tuple(cmd),
                    elapsed_ms=elapsed_ms,
                )
            elapsed_ms = (time.perf_counter() - start) * 1000

        stdout = stdout.strip()
        stderr = stderr.strip()
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

    def _run_worker(self, source: str, profile: str, modules: Sequence[Path],
                    proc_env: Mapping[str, str], data_dir: Path | str | None,
                    timeout: float, explicit_env: Mapping[str, str]) -> DaoResult:
        module_paths = tuple(str(Path(path).resolve()) for path in modules)
        data_key = "" if data_dir is None else str(Path(data_dir).resolve())
        env_key = tuple(sorted((str(key), str(value)) for key, value in explicit_env.items()))
        key = (profile, module_paths, data_key, env_key)
        command = (str(self.binary), "--serve", str(self.bootstrap), *module_paths)
        start = time.perf_counter()
        worker: _CVMWorker | None = None
        try:
            with self._workers_lock:
                deadline = time.monotonic() + timeout
                while worker is None:
                    now = time.monotonic()
                    expired_keys = [
                        worker_key for worker_key, candidate in self._workers.items()
                        if candidate.active_calls == 0
                        and now - candidate.last_used >= self.max_worker_idle_seconds
                    ]
                    for expired_key in expired_keys:
                        self._workers.pop(expired_key).close()

                    candidate = self._workers.get(key)
                    if candidate is not None and candidate.reusable:
                        worker = candidate
                    else:
                        if candidate is not None and candidate.active_calls == 0:
                            self._workers.pop(key)
                            candidate.close()
                        idle = [
                            item for item in self._workers.items()
                            if item[1].active_calls == 0
                        ]
                        if len(self._workers) >= self.max_workers and idle:
                            oldest_key, oldest = min(
                                idle, key=lambda item: item[1].last_used)
                            self._workers.pop(oldest_key)
                            oldest.close()
                        if len(self._workers) < self.max_workers:
                            worker = _CVMWorker(
                                command, proc_env, self.root, timeout,
                                self.max_worker_requests, self.max_private_mb)
                            self._workers[key] = worker
                    if worker is None:
                        remaining = deadline - time.monotonic()
                        if remaining <= 0:
                            raise TimeoutError("C VM worker pool capacity timed out")
                        self._workers_lock.wait(timeout=min(0.1, remaining))
                worker.active_calls += 1

            raw = worker.call(source, timeout)
        except (RuntimeError, TimeoutError, queue.Empty) as exc:
            with self._workers_lock:
                if worker is not None:
                    worker.active_calls = max(0, worker.active_calls - 1)
                failed = self._workers.pop(key, None) if worker is not None else None
                self._workers_lock.notify_all()
            if failed is worker and failed is not None:
                failed.close()
            elapsed_ms = (time.perf_counter() - start) * 1000
            return DaoResult(
                ok=False,
                stderr=worker.stderr if worker is not None else "",
                returncode=-1,
                error=str(exc),
                command=command,
                elapsed_ms=elapsed_ms,
            )

        elapsed_ms = (time.perf_counter() - start) * 1000
        envelope = json.loads(raw)
        private_mb = worker.private_memory_mb()
        recycle = private_mb is not None and private_mb >= self.max_private_mb
        with self._workers_lock:
            worker.active_calls = max(0, worker.active_calls - 1)
            if recycle:
                worker.max_requests = 0
            should_close = recycle and worker.active_calls == 0
            if should_close and self._workers.get(key) is worker:
                self._workers.pop(key)
            self._workers_lock.notify_all()
        if should_close:
            worker.close()
        if not envelope.get("ok"):
            return DaoResult(
                ok=False, error=str(envelope.get("error") or "C VM execution failed"),
                command=command,
                elapsed_ms=elapsed_ms)
        value = envelope.get("value")
        if isinstance(value, str) and _looks_like_runtime_error(value):
            return DaoResult(
                ok=False, value=value, stdout=raw, returncode=0, error=value,
                command=command, elapsed_ms=elapsed_ms)
        return DaoResult(
            ok=True, value=value, stdout=raw, returncode=0, command=command,
            elapsed_ms=elapsed_ms)

    def close(self) -> None:
        with self._workers_lock:
            workers = list(self._workers.values())
            self._workers.clear()
            self._workers_lock.notify_all()
        for worker in workers:
            worker.close()

    def __enter__(self) -> "CVMRuntime":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass

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
        args_json = json.dumps(ordered_args, ensure_ascii=True)
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
