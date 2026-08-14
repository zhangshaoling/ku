"""Native Agent Gateway on the new kernel: think-act-observe-replan loop."""
from __future__ import annotations

import time
from enum import Enum
from typing import Any, Callable, Optional

from .thought import Thought, fnv1a
from .task import Task, TaskPlanner, TaskPriority, TaskStatus


class AgentPhase(Enum):
    THINK = "think"
    ACT = "act"
    OBSERVE = "observe"
    RE_PLAN = "re_plan"


class AgentState:
    """Mutable agent loop state."""

    def __init__(self, goal: str, max_turns: int = 50):
        self.goal = goal
        self.max_turns = max_turns
        self.current_turn = 0
        self.phase = AgentPhase.THINK
        self.completed = False
        self.result: Any = None
        self.error: Optional[str] = None
        self.history: list[dict] = []

    def to_dict(self) -> dict:
        return {
            "goal": self.goal,
            "turn": self.current_turn,
            "max_turns": self.max_turns,
            "phase": self.phase.value,
            "completed": self.completed,
            "result": self.result,
            "error": self.error,
            "history_len": len(self.history),
        }


class Agent:
    """Native agent gateway: runs a think-act-observe-replan loop on the new kernel.

    Tools are Thoughts registered by name. Each tool is called via C ABI.
    The agent plans tasks, executes them, observes results, and re-plans.
    """

    def __init__(self, goal: str, max_turns: int = 50):
        self.state = AgentState(goal, max_turns)
        self.tools: dict[str, Thought] = {}
        self._callbacks: dict[AgentPhase, Callable] = {}
        self._runtime = None

    def register_tool(self, name: str, thought: Thought) -> None:
        """Register a tool (Thought) callable by the agent."""
        self.tools[name] = thought

    def on(self, phase: AgentPhase, callback: Callable) -> None:
        """Register a callback for a loop phase."""
        self._callbacks[phase] = callback

    def run(self, runtime) -> Any:
        """Run the agent loop until completion or max turns."""
        self._runtime = runtime
        try:
            while self.state.current_turn < self.state.max_turns:
                self.state.current_turn += 1

                # THINK
                self.state.phase = AgentPhase.THINK
                thought = self._think()
                self._fire(AgentPhase.THINK, thought)
                if self.state.completed:
                    break

                # ACT
                self.state.phase = AgentPhase.ACT
                action = self._act(thought)
                self._fire(AgentPhase.ACT, action)

                # OBSERVE
                self.state.phase = AgentPhase.OBSERVE
                obs = self._observe(action)
                self._fire(AgentPhase.OBSERVE, obs)

                # RE_PLAN
                self.state.phase = AgentPhase.RE_PLAN
                self._re_plan(obs)

                self.state.history.append({
                    "turn": self.state.current_turn,
                    "thought": thought,
                    "action": action,
                    "observation": obs,
                })

            return self.state.result
        finally:
            self._runtime = None

    def _think(self) -> dict:
        """Decide what to do next based on goal and history."""
        if not self.state.history:
            return {"type": "start", "goal": self.state.goal}
        last = self.state.history[-1]
        if last["observation"].get("success"):
            self.state.completed = True
            self.state.result = last["observation"].get("result")
            return {"type": "finish", "result": self.state.result}
        return {"type": "retry", "goal": self.state.goal}

    def _act(self, thought: dict) -> dict:
        """Execute the thought using a tool (Thought) via C ABI."""
        if thought.get("type") in ("finish",):
            self.state.completed = True
            self.state.result = "done"
            return {"type": "none"}

        tool_name = self._select_tool(thought)
        if tool_name is None:
            return {"type": "no_tool", "thought": thought}

        tool_thought = self.tools.get(tool_name)
        if tool_thought is None:
            return {"type": "tool_not_found", "tool": tool_name}

        try:
            module = self._runtime.load(tool_thought.module_bytes)
            try:
                result = module.call_i64(fnv1a("main"))
                return {"type": "tool_result", "tool": tool_name, "result": result}
            finally:
                module.close()
        except Exception as e:
            return {"type": "tool_error", "tool": tool_name, "error": str(e)}

    def _observe(self, action: dict) -> dict:
        """Observe the result of an action."""
        if action.get("type") == "tool_error":
            return {"success": False, "error": action.get("error")}
        if action.get("type") == "no_tool":
            return {"success": False, "error": "no tool available"}
        if action.get("type") == "tool_result":
            return {"success": True, "result": action.get("result")}
        return {"success": True, "action": action}

    def _re_plan(self, obs: dict) -> None:
        """Re-plan based on observation."""
        if not obs.get("success"):
            self.state.error = obs.get("error")

    def _select_tool(self, thought: dict) -> Optional[str]:
        """Select a tool by matching its name against the goal."""
        if not self.tools:
            return None
        goal = thought.get("goal", "")
        for name in self.tools:
            if name in goal:
                return name
        return next(iter(self.tools))

    def _fire(self, phase: AgentPhase, data: Any) -> None:
        cb = self._callbacks.get(phase)
        if cb:
            cb(data)


class Toolbox:
    """A collection of tool Thoughts that can be registered with an Agent."""

    def __init__(self):
        self.tools: dict[str, Thought] = {}

    def add(self, name: str, thought: Thought) -> None:
        self.tools[name] = thought

    def register_all(self, agent: Agent) -> None:
        for name, thought in self.tools.items():
            agent.register_tool(name, thought)

    @staticmethod
    def from_directory(directory, pattern: str = "*.dao") -> "Toolbox":
        """Load all .dao files from a directory as tools."""
        from pathlib import Path
        tb = Toolbox()
        for p in sorted(Path(directory).glob(pattern)):
            thought = Thought.load(p)
            tb.tools[p.stem] = thought
        return tb
