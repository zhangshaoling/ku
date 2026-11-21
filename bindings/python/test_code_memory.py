import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess
import sys
import threading
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).parent))
from dao_kernel import CodeMemory, DaoKernelError
import dao_kernel.code_memory as code_memory

if len(sys.argv) == 2:
    code_memory._find_dao_ku = lambda: Path(sys.argv[1])


root = Path(tempfile.mkdtemp(prefix="ku_agent_code_memory_"))
try:
    memory = CodeMemory(root)
    identity = "ku:agent/self-check"
    assert "stored" in memory.save_source("思 main() { 返 42 }", identity)
    assert "verified" in memory.verify(identity)
    assert memory.run(identity) == 42
    derived = "ku:agent/self-check-v2"
    assert "stored" in memory.save_derived_source(
        "思 main() { 返 43 }", derived, (2, 0, 0), identity, (1, 0, 0)
    )
    assert memory.verify(derived, (2, 0, 0)).startswith("verified")
    assert memory.run(derived, version=(2, 0, 0)) == 43
    try:
        memory.save_source("思 main() { 返 1 }", "invalid/name")
    except ValueError:
        pass
    else:
        raise AssertionError("invalid identity should be rejected")

    fresh_memory = CodeMemory(root / "fresh")
    try:
        fresh_memory.save_derived_source(
            "思 main() { 返 43 }", "ku:agent/child", (1, 0, 0), identity, (1, 0, 0)
        )
    except DaoKernelError:
        pass
    else:
        raise AssertionError("missing parent must be rejected")

    concurrent_memory = CodeMemory(root / "concurrent")
    callers_ready = threading.Barrier(2)
    compiler_lock = threading.Lock()
    native_run = subprocess.run

    def serialized_compile(command, **kwargs):
        callers_ready.wait(timeout=10)
        with compiler_lock:
            return native_run(command, **kwargs)

    def save_value(value):
        concurrent_memory.save_source(
            f"思 main() {{ 返 {value} }}", f"ku:agent/value-{value}", recovery=True
        )

    # Both calls create their input before either compiler reads it. Native writes
    # are serialized so this checks adapter input isolation independently of locking.
    with patch("dao_kernel.code_memory.subprocess.run", side_effect=serialized_compile):
        with ThreadPoolExecutor(max_workers=2) as callers:
            list(callers.map(save_value, [41, 42]))
    assert concurrent_memory.run("ku:agent/value-41") == 41
    assert concurrent_memory.run("ku:agent/value-42") == 42
    print("agent code memory passed")
finally:
    import shutil
    shutil.rmtree(root, ignore_errors=True)
