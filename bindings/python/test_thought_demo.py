import sys
sys.path.insert(0, "bindings/python")
from dao_kernel import Thought, Runtime, DaoValue, DAO_VALUE_I64, fnv1a
from pathlib import Path

lib = Path("kernel/out/cmake/bin/libdao_kernel.dll")

# Demo: robot_control Thought with host functions
module_bytes = open("demos/robot_control.dao", "rb").read()
ctrl = Thought("robot_control", [], module_bytes, doc="2D robot control loop")

robot = {"x": 0.0, "y": 0.0, "tx": 10.0, "ty": 10.0, "steps": 0}

with Runtime(lib) as rt:
    # Register host functions
    def sensor_distance(args, count):
        import math
        dx = robot["tx"] - robot["x"]
        dy = robot["ty"] - robot["y"]
        return DaoValue(DAO_VALUE_I64, 0, int(math.sqrt(dx*dx + dy*dy) * 100))

    def sensor_bearing(args, count):
        import math
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

    rt.register_host_function(fnv1a("sensor_distance"), 0, sensor_distance)
    rt.register_host_function(fnv1a("sensor_bearing"), 0, sensor_bearing)
    rt.register_host_function(fnv1a("actuate"), 2, actuate)

    # Execute the Thought
    result = ctrl.call_i64(rt, args=[])
    print(f"Result: {result}")
    print(f"Robot position: ({robot['x']:.3f}, {robot['y']:.3f})")
    print(f"Steps: {robot['steps']}")
    print(f"Thought executions: {ctrl.meta['executions']}")

print("Thought + host FFI demo passed")