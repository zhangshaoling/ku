import sys
sys.path.insert(0, "bindings/python")
from dao_kernel.thought import Thought

# Test 1: Create from compiled module bytes
module_bytes = open("demos/robot_control.dao", "rb").read()
t = Thought("robot_control", [], module_bytes, doc="Robot control loop")
print(f"Created: {t}")
print(f"Bytes: {len(t.to_bytes())}")

# Test 2: from_bytes roundtrip
data = t.to_bytes()
t2 = Thought.from_bytes("robot_control_v2", data, params=[])
print(f"Roundtrip: {t2}")
print(f"Bytes match: {t2.to_bytes() == data}")

# Test 3: Registry
print(f"Registry: {list(Thought.registry.keys())}")

# Test 4: Compile from source (no params)
t3 = Thought.from_source("answer", "thought main() { return 42 }\n", doc="The answer")
print(f"Compiled from source: {t3}")
result = t3.call_i64(args=[])
print(f"answer() result: {result}")
assert result == 42, f"expected 42, got {result}"

# Test 5: Compile with params
t4 = Thought.from_source("add_xy", "thought main(x, y) { return x + y }\n", params=["x", "y"])
result2 = t4.call_i64(args=[10, 20])
print(f"add_xy(10, 20) result: {result2}")
assert result2 == 30, f"expected 30, got {result2}"

# Test 6: Exec counter
print(f"Executions: {t3.meta['executions']}")
assert t3.meta["executions"] == 1

print("All Thought tests passed")