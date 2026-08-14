import sys, tempfile
from pathlib import Path
sys.path.insert(0, "bindings/python")
from dao_kernel import Thought, Task, TaskPlanner, TaskPriority, TaskStatus, Runtime, fnv1a

def test_task_basic():
    """Task lifecycle: pending -> running -> completed/failed."""
    module_bytes = open("demos/robot_control.dao", "rb").read()
    thought = Thought("robot_control", [], module_bytes, doc="robot")

    task = Task("move", thought, TaskPriority.HIGH, input_args=[])
    assert task.status == TaskStatus.PENDING
    assert task.name == "move"
    assert task.priority == TaskPriority.HIGH

    d = task.to_dict()
    assert d["name"] == "move"
    assert d["status"] == "pending"

    print("test_task_basic passed")


def test_planner_deps():
    """TaskPlanner respects dependency order."""
    module_bytes = open("demos/robot_control.dao", "rb").read()
    thought = Thought("robot_control", [], module_bytes, doc="robot")

    t1 = Task("init", thought, TaskPriority.HIGH)
    t2 = Task("plan", thought, TaskPriority.HIGH, deps=["init"])
    t3 = Task("execute", thought, TaskPriority.NORMAL, deps=["plan"])

    planner = TaskPlanner()
    planner.add_task(t1)
    planner.add_task(t2)
    planner.add_task(t3)

    # Only "init" should be ready (deps met)
    ready = planner.get_ready_tasks()
    assert len(ready) == 1
    assert ready[0].name == "init"

    # Complete "init", now "plan" should be ready
    planner.complete_task("init", result=0)
    ready = planner.get_ready_tasks()
    assert len(ready) == 1
    assert ready[0].name == "plan"

    # Complete all
    planner.complete_task("plan", result=0)
    planner.complete_task("execute", result=42)

    progress = planner.get_progress()
    assert progress["total"] == 3
    assert progress["completed"] == 3
    assert progress["pending"] == 0

    print("test_planner_deps passed")


def test_planner_execute():
    """TaskPlanner.execute runs tasks via C ABI."""
    module_bytes = open("demos/robot_control.dao", "rb").read()
    thought = Thought("robot_control", [], module_bytes, doc="robot")

    planner = TaskPlanner()
    planner.add_task(Task("run", thought, TaskPriority.HIGH, input_args=[]))

    dll = Path("kernel/out/cmake/bin/libdao_kernel.dll")
    with Runtime(dll) as rt:
        # Register stub host functions
        def stub(args, count):
            from dao_kernel import DaoValue, DAO_VALUE_I64
            return DaoValue(DAO_VALUE_I64, 0, 0)
        rt.register_host_function(fnv1a("sensor_distance"), 0, stub)
        rt.register_host_function(fnv1a("sensor_bearing"), 0, stub)
        rt.register_host_function(fnv1a("actuate"), 2, stub)

        executed = planner.execute(rt)
        assert len(executed) == 1
        assert executed[0].status == TaskStatus.COMPLETED
        assert isinstance(executed[0].result, int)

    print("test_planner_execute passed")


def test_planner_priority():
    """Ready tasks are sorted by priority."""
    module_bytes = open("demos/robot_control.dao", "rb").read()
    thought = Thought("robot_control", [], module_bytes, doc="robot")

    t1 = Task("low", thought, TaskPriority.LOW)
    t2 = Task("critical", thought, TaskPriority.CRITICAL)
    t3 = Task("normal", thought, TaskPriority.NORMAL)

    planner = TaskPlanner()
    planner.add_task(t1)
    planner.add_task(t2)
    planner.add_task(t3)

    ready = planner.get_ready_tasks()
    assert len(ready) == 3
    assert ready[0].name == "critical"
    assert ready[1].name == "normal"
    assert ready[2].name == "low"

    print("test_planner_priority passed")


def test_planner_fail():
    """Failed tasks are tracked."""
    planner = TaskPlanner()
    planner.add_task(Task("t1", Thought("x", [], b"")))
    planner.fail_task("t1", error="something went wrong")

    progress = planner.get_progress()
    assert progress["failed"] == 1
    assert progress["completed"] == 0

    print("test_planner_fail passed")


def test_decompose():
    """Goal decomposition produces task lists."""
    tasks = TaskPlanner.decompose("move robot to target")
    assert len(tasks) >= 2
    names = [t.name for t in tasks]
    assert "init" in names

    tasks = TaskPlanner.decompose("generic goal")
    assert len(tasks) == 2

    print("test_decompose passed")


if __name__ == "__main__":
    test_task_basic()
    test_planner_deps()
    test_planner_execute()
    test_planner_priority()
    test_planner_fail()
    test_decompose()
    print("All task tests passed")
