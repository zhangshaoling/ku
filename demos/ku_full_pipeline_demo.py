"""Full pipeline demo: compile -> store -> recall -> execute -> observe.

Demonstrates the complete Ku thought lifecycle on the new kernel:
1. Compile .ku source to .dao binary
2. Store as executable memory (MemorySystem)
3. Recall and register with Agent as a tool
4. Agent runs think-act-observe-replan loop
5. Robot reaches target, agent observes success
"""
import sys
import math
sys.path.insert(0, "bindings/python")
from pathlib import Path
from dao_kernel import (
    Thought, MemorySystem, MemoryType, Task, TaskPlanner,
    TaskPriority, Agent, AgentPhase, Toolbox, Runtime,
    DaoValue, DAO_VALUE_I64, fnv1a
)

DLL = Path("kernel/out/cmake/bin/libdao_kernel.dll")

# ── Step 1: Compile .ku to .dao ──
print("=== Step 1: Compile ===")
ku_source = """import sensor_distance(0)
import sensor_bearing(0)
import actuate(2)

thought main() {
  d = sensor_distance()
  while d >= 50 {
    b = sensor_bearing()
    ax = 30
    ay = 30
    actuate(ax, ay)
    d = sensor_distance()
  }
  return d
}
"""
import tempfile, subprocess
with tempfile.NamedTemporaryFile(suffix=".ku", mode="w", encoding="utf-8", delete=False) as f:
    f.write(ku_source)
    ku_path = f.name
dao_path = ku_path.replace(".ku", ".dao")
subprocess.run([str(Path("kernel/out/cmake/bin/dao-ku.exe")), ku_path, dao_path], check=True)
thought = Thought.from_source("robot_control", ku_source)
print(f"Compiled: {thought.name}, {len(thought.module_bytes)} bytes")

# ── Step 2: Store in MemorySystem ──
print("\n=== Step 2: Store in Memory ===")
mem = MemorySystem(tempfile.mkdtemp())
mem.store("robot_control", thought, MemoryType.LONG_TERM,
          meta={"domain": "robotics", "version": "1.0"})
print(f"Stored. Memory stats: {mem.stats()}")

# ── Step 3: Recall and prepare for execution ──
print("\n=== Step 3: Recall ===")
recalled = mem.recall("robot_control")
print(f"Recalled: {recalled.name}, executions={recalled.meta['executions']}")

# ── Step 4: Agent runs the tool ──
print("\n=== Step 4: Agent Execution ===")
robot = {"x": 0.0, "y": 0.0, "tx": 10.0, "ty": 10.0, "steps": 0}

def sensor_distance(args, count):
    dx = robot["tx"] - robot["x"]
    dy = robot["ty"] - robot["y"]
    return DaoValue(DAO_VALUE_I64, 0, int(math.sqrt(dx*dx + dy*dy) * 100))

def sensor_bearing(args, count):
    dx = robot["tx"] - robot["x"]
    dy = robot["ty"] - robot["y"]
    return DaoValue(DAO_VALUE_I64, 0, int(math.atan2(dy, dx) * 100))

def actuate(args, count):
    ax = args[0].payload / 100.0
    ay = args[1].payload / 100.0
    robot["x"] += ax * 0.1
    robot["y"] += ay * 0.1
    robot["steps"] += 1
    return DaoValue(DAO_VALUE_I64, 0, 0)

agent = Agent("navigate robot to target", max_turns=5)
agent.register_tool("robot_control", recalled)

# Add phase logging
for phase in AgentPhase:
    agent.on(phase, lambda d, p=phase: print(f"  [{p.value}] {d.get('type', '?')}"))

with Runtime(DLL) as rt:
    rt.register_host_function(fnv1a("sensor_distance"), 0, sensor_distance)
    rt.register_host_function(fnv1a("sensor_bearing"), 0, sensor_bearing)
    rt.register_host_function(fnv1a("actuate"), 2, actuate)
    result = agent.run(rt)

# ── Step 5: Observe results ──
print("\n=== Step 5: Results ===")
print(f"Agent result: {result}")
print(f"Robot position: ({robot['x']:.3f}, {robot['y']:.3f})")
print(f"Steps taken: {robot['steps']}")
print(f"Agent turns: {agent.state.current_turn}")
print(f"History entries: {len(agent.state.history)}")

# Verify robot reached target
dist = math.sqrt((robot["tx"] - robot["x"])**2 + (robot["ty"] - robot["y"])**2)
print(f"Distance to target: {dist:.3f}")
assert dist < 5.0, f"Robot did not reach target! dist={dist}"
print("\nFull pipeline demo PASSED")
