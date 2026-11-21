"""End-to-end test for native Ku 存/取/忘 memory Host imports."""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from dao_kernel import DaoValue, DAO_VALUE_I64, Runtime, fnv1a
from dao_kernel.memory_host import MemoryHost


ROOT = Path(__file__).resolve().parents[2]
LIBRARY = ROOT / "kernel" / "out" / "cmake" / "bin" / "libdao_kernel.dll"
COMPILER = ROOT / "kernel" / "out" / "cmake" / "bin" / "dao-ku.exe"


def test_runtime_close_without_host_callbacks():
    runtime = Runtime(LIBRARY)
    runtime.close()
    runtime.close()
    assert runtime._vm is None


def test_memory_host_roundtrip():
    with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryDirectory() as build:
        source = Path(build) / "memory.ku"
        module_path = Path(build) / "memory.dao"
        source.write_text(
            '思 main() { 存 "answer" 42 返 取 "answer" }\n'
            '思 recall() { 返 取 "answer" }\n'
            '思 forget() { 返 忘 "answer" }\n',
            encoding="utf-8",
        )
        subprocess.run([str(COMPILER), str(source), str(module_path)], check=True, capture_output=True)
        with Runtime(LIBRARY) as runtime:
            host = MemoryHost(runtime, directory)
            module = runtime.load(module_path.read_bytes())
            try:
                assert module.call_i64(fnv1a("main")) == 42
            finally:
                module.close()
            host.close()
        with Runtime(LIBRARY) as runtime:
            host = MemoryHost(runtime, directory)
            assert host.values["answer"] == 42
            host.close()
        child = """
import sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
from dao_kernel import Runtime, fnv1a
from dao_kernel.memory_host import MemoryHost
with Runtime(sys.argv[2]) as runtime:
    host = MemoryHost(runtime, sys.argv[3])
    with runtime.load(Path(sys.argv[4]).read_bytes()) as module:
        assert module.call_i64(fnv1a("recall")) == 42
        assert module.call_i64(fnv1a("forget")) == 1
        assert module.call_i64(fnv1a("recall")) == 0
        assert module.call_i64(fnv1a("forget")) == 0
    host.close()
"""
        subprocess.run(
            [sys.executable, "-c", child, str(Path(__file__).parent),
             str(LIBRARY), directory, str(module_path)],
            check=True, capture_output=True,
        )
        with Runtime(LIBRARY) as runtime:
            host = MemoryHost(runtime, directory)
            assert "answer" not in host.values
            host.close()


def test_memory_host_forget_result():
    with tempfile.TemporaryDirectory() as directory:
        with Runtime(LIBRARY) as runtime:
            host = MemoryHost(runtime, directory)
            host._store([runtime.make_string("x"), DaoValue(DAO_VALUE_I64, 0, 7)], 2)
            assert host.values["x"] == 7
            assert host._forget([runtime.make_string("x")], 1).payload == 1
            assert host._forget([runtime.make_string("x")], 1).payload == 0
            host.close()


if __name__ == "__main__":
    if len(sys.argv) == 3:
        LIBRARY, COMPILER = map(Path, sys.argv[1:])
    test_runtime_close_without_host_callbacks()
    test_memory_host_roundtrip()
    test_memory_host_forget_result()
    print("memory Host tests passed")
