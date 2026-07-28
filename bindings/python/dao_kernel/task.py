"""Task loop on the new kernel: tasks are Thoughts + execution planning."""
from __future__ import annotations

import time
from enum import IntEnum
from typing import Any, Optional

from .thought import Thought, fnv1a


class TaskPriority(IntEnum):
    CRITICAL = 0
    HIGH = 1
    NORMAL = 2
    LOW = 3
    BACKGROUND = 4


class TaskStatus:
    PENDING = "pending"
    RUNNING = "running"
    COMPLETED = "completed"
    FAILED = "failed"


class Task:
    """A unit of work: an executable Thought + metadata."""

    def __init__(self, name: str, thought: Thought,
                 priority: TaskPriority = TaskPriority.NORMAL,
                 deps: Optional[list[str]] = None,
                 input_args: Optional[list[int]] = None,
                 meta: Optional[dict] = None):
        self.name = name
        self.thought = thought
        self.priority = priority
        self.deps = deps or []
        self.input_args = input_args or []
        self.meta = meta or {}
        self.status = TaskStatus.PENDING
        self.result: Any = None
        self.error: Optional[str] = None
        self.created_at = time.time()
        self.started_at: Optional[float] = None
        self.completed_at: Optional[float] = None

    def to_dict(self) -> dict:
        return {
            "name": self.name,
            "priority": self.priority.value,
            "status": self.status,
            "deps": self.deps,
            "result": self.result,
            "error": self.error,
        }


class TaskPlanner:
    """Plan and execute tasks on the new kernel.

    Tasks are executed via the C ABI Runtime. Each task's Thought is loaded
    and called with its input_args. Results feed back into the planner.
    """

    def __init__(self):
        self.tasks: dict[str, Task] = {}

    def add_task(self, task: Task) -> None:
        self.tasks[task.name] = task

    def get_ready_tasks(self) -> list[Task]:
        """Return tasks whose dependencies are all completed, sorted by priority."""
        ready = []
        for task in self.tasks.values():
            if task.status != TaskStatus.PENDING:
                continue
            deps_met = all(
                self.tasks.get(d, Task(d, Thought("stub", [], b""))).status == TaskStatus.COMPLETED
                for d in task.deps
            )
            if deps_met:
                ready.append(task)
        ready.sort(key=lambda t: t.priority.value)
        return ready

    def execute(self, runtime) -> list[Task]:
        """Execute all ready tasks. Returns list of tasks that ran."""
        executed = []
        for task in self.get_ready_tasks():
            task.status = TaskStatus.RUNNING
            task.started_at = time.time()
            try:
                module = runtime.load(task.thought.module_bytes)
                try:
                    result = module.call_i64(fnv1a("main"), *task.input_args)
                    task.result = result
                    task.status = TaskStatus.COMPLETED
                finally:
                    module.close()
            except Exception as e:
                task.error = str(e)
                task.status = TaskStatus.FAILED
            task.completed_at = time.time()
            executed.append(task)
        return executed

    def complete_task(self, name: str, result: Any = None) -> None:
        """Mark a task as completed (for external execution)."""
        if name in self.tasks:
            self.tasks[name].status = TaskStatus.COMPLETED
            self.tasks[name].result = result
            self.tasks[name].completed_at = time.time()

    def fail_task(self, name: str, error: str = "") -> None:
        if name in self.tasks:
            self.tasks[name].status = TaskStatus.FAILED
            self.tasks[name].error = error

    def get_progress(self) -> dict:
        total = len(self.tasks)
        completed = sum(1 for t in self.tasks.values() if t.status == TaskStatus.COMPLETED)
        failed = sum(1 for t in self.tasks.values() if t.status == TaskStatus.FAILED)
        pending = total - completed - failed
        return {"total": total, "completed": completed, "failed": failed, "pending": pending}

    @staticmethod
    def decompose(goal: str) -> list[Task]:
        """Decompose a goal into tasks (stub Thoughts for now)."""
        tasks = []
        goal_lower = goal.lower()
        if "robot" in goal_lower or "move" in goal_lower:
            stub = Thought("robot_control", [], b"", doc="robot control")
            tasks.append(Task("init", stub, TaskPriority.HIGH))
            tasks.append(Task("sense", stub, TaskPriority.HIGH, deps=["init"]))
            tasks.append(Task("act", stub, TaskPriority.NORMAL, deps=["sense"]))
        else:
            stub = Thought("generic", [], b"", doc="generic task")
            tasks.append(Task("understand", stub, TaskPriority.HIGH))
            tasks.append(Task("execute", stub, TaskPriority.NORMAL, deps=["understand"]))
        return tasks
