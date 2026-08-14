import sys, tempfile
from pathlib import Path
sys.path.insert(0, "bindings/python")
from dao_kernel import Thought, MemorySystem, MemoryType, Runtime, fnv1a

def test_memory_basic():
    """Basic store/recall/forget cycle."""
    with tempfile.TemporaryDirectory() as tmp:
        mem = MemorySystem(tmp)

        # Create a thought and store it
        module_bytes = open("demos/robot_control.dao", "rb").read()
        thought = Thought("robot_control", [], module_bytes, doc="2D robot control")
        entry = mem.store("robot_v1", thought, MemoryType.LONG_TERM, meta={"tags": ["robot"]})

        assert "robot_v1" in mem
        assert len(mem) == 1
        assert entry.type == MemoryType.LONG_TERM

        # Recall
        recalled = mem.recall("robot_v1")
        assert recalled is not None
        assert recalled.name == "robot_control"
        assert entry.access_count == 1

        # Forget
        assert mem.forget("robot_v1") is True
        assert "robot_v1" not in mem
        assert len(mem) == 0

        # Forget nonexistent
        assert mem.forget("nonexistent") is False

    print("test_memory_basic passed")


def test_memory_persistence():
    """Memories survive MemorySystem restart."""
    with tempfile.TemporaryDirectory() as tmp:
        # Store
        mem1 = MemorySystem(tmp)
        module_bytes = open("demos/robot_control.dao", "rb").read()
        thought = Thought("robot_control", [], module_bytes, doc="2D robot control")
        mem1.store("robot_v1", thought, MemoryType.LONG_TERM, meta={"tags": ["robot"]})
        assert len(mem1) == 1

        # Reload
        mem2 = MemorySystem(tmp)
        assert len(mem2) == 1
        assert "robot_v1" in mem2
        recalled = mem2.recall("robot_v1")
        assert recalled is not None
        assert recalled.name == "robot_control"

    print("test_memory_persistence passed")


def test_memory_search():
    """Search memories by key/name/doc."""
    with tempfile.TemporaryDirectory() as tmp:
        mem = MemorySystem(tmp)
        module_bytes = open("demos/robot_control.dao", "rb").read()

        t1 = Thought("robot_control", [], module_bytes, doc="2D robot control")
        t2 = Thought("answer", [], module_bytes, doc="returns 42")
        t3 = Thought("arithmetic", ["x", "y"], module_bytes, doc="math operations")

        mem.store("robot_v1", t1, MemoryType.LONG_TERM, meta={"tags": ["robot"]})
        mem.store("answer_42", t2, MemoryType.FACT, meta={"tags": ["test"]})
        mem.store("math_ops", t3, MemoryType.SESSION, meta={"tags": ["math"]})

        # Search by key
        results = mem.search("robot")
        assert len(results) >= 1
        assert results[0].key == "robot_v1"

        # Search by doc
        results = mem.search("math")
        assert len(results) >= 1

        # Search all
        results = mem.search("")
        assert len(results) == 3

    print("test_memory_search passed")


def test_memory_stats():
    """Statistics reporting."""
    with tempfile.TemporaryDirectory() as tmp:
        mem = MemorySystem(tmp)
        module_bytes = open("demos/robot_control.dao", "rb").read()

        t1 = Thought("robot_control", [], module_bytes, doc="robot")
        t2 = Thought("answer", [], module_bytes, doc="answer")

        mem.store("r1", t1, MemoryType.LONG_TERM)
        mem.store("a1", t2, MemoryType.FACT)

        stats = mem.stats()
        assert stats["total"] == 2
        assert "long_term" in stats["by_type"]
        assert "fact" in stats["by_type"]

    print("test_memory_stats passed")


def test_memory_executable():
    """Stored memories are executable Thoughts."""
    with tempfile.TemporaryDirectory() as tmp:
        mem = MemorySystem(tmp)
        module_bytes = open("demos/robot_control.dao", "rb").read()
        thought = Thought("robot_control", [], module_bytes, doc="robot")
        mem.store("robot", thought, MemoryType.LONG_TERM)

        # Recall and execute
        recalled = mem.recall("robot")
        assert recalled is not None

        dll = Path("kernel/out/cmake/bin/libdao_kernel.dll")
        with Runtime(dll) as rt:
            # Register stub host functions
            def stub(args, count):
                from dao_kernel import DaoValue, DAO_VALUE_I64
                return DaoValue(DAO_VALUE_I64, 0, 0)
            rt.register_host_function(fnv1a("sensor_distance"), 0, stub)
            rt.register_host_function(fnv1a("sensor_bearing"), 0, stub)
            rt.register_host_function(fnv1a("actuate"), 2, stub)

            module = rt.load(recalled.module_bytes)
            try:
                result = module.call_i64(fnv1a("main"))
                assert isinstance(result, int)
            finally:
                module.close()

    print("test_memory_executable passed")


if __name__ == "__main__":
    test_memory_basic()
    test_memory_persistence()
    test_memory_search()
    test_memory_stats()
    test_memory_executable()
    print("All memory tests passed")
