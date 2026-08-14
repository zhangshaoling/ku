import sys, tempfile
from pathlib import Path
sys.path.insert(0, "bindings/python")
from dao_kernel import Thought

# Test save/load roundtrip
t1 = Thought.from_source("arithmetic", "thought main(x, y) { return x * y + 1 }\n", params=["x", "y"])
result1 = t1.call_i64(args=[3, 4])
assert result1 == 13, f"expected 13, got {result1}"
print(f"Original: {t1} -> call(3,4) = {result1}")

# Save to temp file
with tempfile.TemporaryDirectory() as tmpdir:
    save_path = Path(tmpdir) / "arithmetic.dao"
    t1.save(save_path)
    print(f"Saved to {save_path} ({save_path.stat().st_size} bytes)")

    # Load back
    t2 = Thought.load(save_path, doc="Loaded from disk")
    result2 = t2.call_i64(args=[3, 4])
    assert result2 == 13, f"expected 13, got {result2}"
    print(f"Loaded: {t2} -> call(3,4) = {result2}")

    # Test scan: save multiple thoughts
    t3 = Thought.from_source("constant", "thought main() { return 42 }\n")
    t3.save(Path(tmpdir) / "constant.dao")

    t4 = Thought.from_source("negate", "thought main(x) { return 0 - x }\n", params=["x"])
    t4.save(Path(tmpdir) / "negate.dao")

    thoughts = Thought.scan(tmpdir)
    print(f"Scanned {len(thoughts)} thoughts: {[t.name for t in thoughts]}")
    assert len(thoughts) == 3

    for t in thoughts:
        print(f"  {t.name}: execs={t.meta['executions']}")

print("All persistence tests passed")