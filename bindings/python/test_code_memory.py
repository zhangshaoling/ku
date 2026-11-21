import tempfile
from pathlib import Path

from dao_kernel import CodeMemory


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
    print("agent code memory passed")
finally:
    import shutil
    shutil.rmtree(root, ignore_errors=True)
