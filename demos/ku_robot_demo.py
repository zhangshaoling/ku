"""Ku -> C ABI -> Robot Simulator demo.

Demonstrates "thought = code = memory" in a robotics context.

Run: python demos/ku_robot_demo.py
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "bindings" / "python"))

from dao_kernel import Runtime, DaoValue, DAO_VALUE_I64
from robot_simulator import make_simulator


def find_kernel_library():
    for pattern in ["libdao_kernel.dll", "dao_kernel.dll"]:
        matches = list((ROOT / "kernel" / "out").rglob(pattern))
        if matches:
            return matches[0]
    return None


def fnv1a(name):
    """Compute the Ku migration compiler''s FNV-1a symbol ID."""
    h = 2166136261
    for c in name.encode():
        h ^= c
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def main():
    library = find_kernel_library()
    if library is None:
        print("SKIP: kernel library not built. Run: .\\tools\\build_kernel.ps1")
        return

    robot = make_simulator()
    module_path = ROOT / "demos" / "robot_control.dao"
    ku_path = ROOT / "demos" / "robot_control.ku"

    if not module_path.exists() and ku_path.exists():
        print("Compiling Ku source...")
        import subprocess
        subprocess.run([
            str(ROOT / "kernel" / "out" / "cmake" / "bin" / "dao-ku.exe"),
            str(ku_path), str(module_path),
        ], check=True)

    if not module_path.exists():
        print("SKIP: no compiled module.")
        return

    with Runtime(library) as rt:
        # Register sensor/actuator host functions
        call_count = [0]

        def sensor_distance(args, count):
            d = robot.sensor_distance_to_target()
            call_count[0] += 1
            print(f"  [host] #{call_count[0]} sensor_distance = {d:.2f}")
            return DaoValue(DAO_VALUE_I64, 0, int(d * 100))

        def sensor_bearing(args, count):
            b = robot.sensor_target_bearing()
            print(f"  [host] #{call_count[0]} sensor_bearing = {b:.2f}")
            return DaoValue(DAO_VALUE_I64, 0, int(b * 100))

        def actuate(args, count):
            ax = args[0].payload / 100.0
            ay = args[1].payload / 100.0
            print(f"  [host] #{call_count[0]} actuate ax={ax:.2f} ay={ay:.2f}")
            robot.actuate(ax, ay)
            return DaoValue(DAO_VALUE_I64, 0, 0)

        rt.register_host_function(fnv1a("sensor_distance"), 0, sensor_distance)
        rt.register_host_function(fnv1a("sensor_bearing"), 0, sensor_bearing)
        rt.register_host_function(fnv1a("actuate"), 2, actuate)

        # Load the compiled Ku module
        module_bytes = module_path.read_bytes()
        module = rt.load(module_bytes)

        print("=== Ku Robot Demo ===")
        print(f"Start: ({robot.x}, {robot.y})  Target: ({robot.target_x}, {robot.target_y})")
        print(f"Initial distance: {robot.sensor_distance_to_target():.2f}")
        print()

        # Execute the Ku control loop
        result = module.call_i64(fnv1a("main"))
        final_distance = result / 100.0

        print()
        print(f"Final position: ({robot.x:.3f}, {robot.y:.3f})")
        print(f"Final distance: {final_distance:.3f}")
        print(f"Steps taken: {robot.step_count}")
        print(f"Distance traveled: {robot.memory_distance_traveled():.3f}")
        print()
        print("=== Demo complete ===")


if __name__ == "__main__":
    main()