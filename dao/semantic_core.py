"""Minimal semantic core for the Dao AGI mother tongue.

This module is intentionally small and side-effect-light.  It gives AI agents a
stable structure to operate on before the textual syntax is expanded further.
"""

from __future__ import annotations

from copy import deepcopy
from dataclasses import dataclass, field
from time import time
from typing import Any, Callable, Iterable, Mapping, Optional, Sequence, Union
from uuid import uuid4

from .runtime import Node, Thought


PathPart = Union[str, int]
AstPath = tuple[PathPart, ...]


def value_to_node(value: Any) -> Node:
    """Convert Python data into a Dao AST value node."""
    if isinstance(value, Node):
        return value
    if isinstance(value, Mapping):
        return Node(
            "dict",
            "",
            [Node("pair", str(key), [value_to_node(val)]) for key, val in value.items()],
        )
    if isinstance(value, (list, tuple)):
        return Node("list", "", [value_to_node(item) for item in value])
    return Node.lit(value)


def node_to_dict(node: Any) -> Any:
    """Serialize a Node or plain AST object without assuming every child is a Node."""
    if isinstance(node, Node):
        return {
            "type": node.type,
            "value": node.value,
            "children": [node_to_dict(child) for child in node.children],
            "meta": dict(node.meta),
        }
    if isinstance(node, Mapping):
        result = {key: node_to_dict(value) for key, value in node.items()}
        result.setdefault("children", [])
        return result
    if isinstance(node, list):
        return [node_to_dict(item) for item in node]
    return node


def ref(name: str) -> Node:
    return Node.ref(name)


def lit(value: Any) -> Node:
    return Node.lit(value)


def call(name: Union[str, Node], args: Iterable[Any] = ()) -> Node:
    callee = name if isinstance(name, Node) else ref(name)
    return Node("call", "", [callee, *[value_to_node(arg) for arg in args]])


def block(statements: Iterable[Any]) -> Node:
    return Node.block([value_to_node(stmt) if not isinstance(stmt, Node) else stmt for stmt in statements])


def thought_ast(name: str, steps: Iterable[Any], params: Sequence[str] = (),
                meta: Optional[Mapping[str, Any]] = None) -> dict[str, Any]:
    """Build the canonical dict AST shape used by the current parser/compiler."""
    body = block(expand_step(step) for step in steps)
    result: dict[str, Any] = {
        "type": "thought",
        "value": name,
        "children": [*params, node_to_dict(body)],
    }
    if meta:
        result["meta"] = dict(meta)
    return result


def thought_from_ast(ast: Mapping[str, Any], register: bool = True,
                     doc: str = "") -> Thought:
    """Create a runtime Thought from the canonical dict AST shape."""
    if ast.get("type") != "thought":
        raise ValueError("thought AST must have type='thought'")
    name = ast.get("value")
    if not isinstance(name, str) or not name:
        raise ValueError("thought AST requires a non-empty name")

    children = list(ast.get("children", []))
    if not children:
        raise ValueError("thought AST requires a body child")

    raw_params = children[:-1]
    if not all(isinstance(param, str) for param in raw_params):
        raise ValueError("thought AST params must be strings")
    body_ast = children[-1]

    from .dao_parser import dict_to_node

    meta = dict(ast.get("meta", {}))
    if not register:
        temp_name = "__semantic_tmp_" + uuid4().hex
        thought = Thought(temp_name, list(raw_params), dict_to_node(body_ast), doc=doc, meta=meta)
        Thought.registry.pop(temp_name, None)
        thought.name = name
        return thought

    return Thought(name, list(raw_params), dict_to_node(body_ast), doc=doc, meta=meta)


def expand_step(step: Any) -> Node:
    """Expand a compact AI-oriented step into an executable call node.

    Supported forms:
    - "observe tests.fail" -> observe("tests.fail")
    - "verify" -> verify()
    - {"patch": {"scope": "minimal"}} -> patch({"scope": "minimal"})
    - {"locate": ["cause", {"mode": "causal"}]} -> locate("cause", {"mode": "causal"})
    """
    if isinstance(step, Node):
        return step
    if isinstance(step, str):
        head, sep, tail = step.strip().partition(" ")
        if not head:
            raise ValueError("empty semantic step")
        return call(head, [tail] if sep else [])
    if isinstance(step, Mapping):
        if len(step) != 1:
            raise ValueError("mapping step must contain exactly one operation")
        name, payload = next(iter(step.items()))
        if payload is None:
            args: list[Any] = []
        elif isinstance(payload, (list, tuple)):
            args = list(payload)
        else:
            args = [payload]
        return call(str(name), args)
    raise TypeError(f"unsupported semantic step: {type(step).__name__}")


@dataclass(frozen=True)
class Effect:
    kind: str
    target: str
    payload: Any = None
    meta: dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        return {
            "kind": self.kind,
            "target": self.target,
            "payload": self.payload,
            "meta": dict(self.meta),
        }


@dataclass(frozen=True)
class TraceEvent:
    effect: Effect
    thought: str = ""
    node: Any = None
    result: Any = None
    ok: bool = True
    created_at: float = field(default_factory=time)

    def to_dict(self) -> dict[str, Any]:
        return {
            "time": self.created_at,
            "thought": self.thought,
            "effect": self.effect.to_dict(),
            "node": node_to_dict(self.node) if self.node is not None else None,
            "result": self.result,
            "ok": self.ok,
        }


@dataclass
class Trace:
    trace_id: str = field(default_factory=lambda: uuid4().hex)
    events: list[TraceEvent] = field(default_factory=list)

    def record(self, effect: Effect, thought: str = "", node: Any = None,
               result: Any = None, ok: bool = True) -> TraceEvent:
        event = TraceEvent(effect=effect, thought=thought, node=node, result=result, ok=ok)
        self.events.append(event)
        return event

    def to_dict(self) -> dict[str, Any]:
        return {"id": self.trace_id, "events": [event.to_dict() for event in self.events]}


@dataclass(frozen=True)
class Patch:
    op: str
    path: AstPath
    value: Any = None
    before: Any = None
    reason: str = ""

    @classmethod
    def replace(cls, path: Sequence[PathPart], value: Any, before: Any = None,
                reason: str = "") -> "Patch":
        return cls("replace", tuple(path), value=value, before=before, reason=reason)

    @classmethod
    def add(cls, path: Sequence[PathPart], value: Any, reason: str = "") -> "Patch":
        return cls("add", tuple(path), value=value, reason=reason)

    @classmethod
    def remove(cls, path: Sequence[PathPart], before: Any = None, reason: str = "") -> "Patch":
        return cls("remove", tuple(path), before=before, reason=reason)

    def apply(self, ast: Any) -> Any:
        target = deepcopy(ast)
        parent, key = _resolve_parent(target, self.path)
        if self.op == "replace":
            _set_child(parent, key, deepcopy(self.value))
        elif self.op == "add":
            _add_child(parent, key, deepcopy(self.value))
        elif self.op == "remove":
            _remove_child(parent, key)
        else:
            raise ValueError(f"unknown patch op: {self.op}")
        return target

    def inverse(self) -> "Patch":
        if self.op == "replace":
            return Patch.replace(self.path, self.before, before=self.value, reason="inverse:" + self.reason)
        if self.op == "add":
            return Patch.remove(self.path, before=self.value, reason="inverse:" + self.reason)
        if self.op == "remove":
            return Patch.add(self.path, self.before, reason="inverse:" + self.reason)
        raise ValueError(f"unknown patch op: {self.op}")

    def to_effect(self) -> Effect:
        return Effect("patch", ".".join(str(part) for part in self.path), {
            "op": self.op,
            "value": self.value,
            "before": self.before,
            "reason": self.reason,
        })


def _resolve_parent(root: Any, path: AstPath) -> tuple[Any, PathPart]:
    if not path:
        raise ValueError("patch path must not be empty")
    current = root
    for part in path[:-1]:
        current = current[part]
    return current, path[-1]


def _set_child(parent: Any, key: PathPart, value: Any) -> None:
    parent[key] = value


def _add_child(parent: Any, key: PathPart, value: Any) -> None:
    if isinstance(parent, list):
        parent.insert(int(key), value)
    else:
        parent[key] = value


def _remove_child(parent: Any, key: PathPart) -> None:
    if isinstance(parent, list):
        del parent[int(key)]
    else:
        del parent[key]


@dataclass
class Memory:
    key: str
    value: Any
    kind: str = "session"
    meta: dict[str, Any] = field(default_factory=dict)
    created_at: float = field(default_factory=time)
    updated_at: float = field(default_factory=time)

    def update(self, value: Any) -> None:
        self.value = value
        self.updated_at = time()

    def to_dict(self) -> dict[str, Any]:
        return {
            "key": self.key,
            "kind": self.kind,
            "value": self.value,
            "meta": dict(self.meta),
            "created_at": self.created_at,
            "updated_at": self.updated_at,
        }


@dataclass
class ToolSpec:
    name: str
    handler: Callable[..., Any]
    description: str = ""
    risk: str = "safe"
    effect_kind: str = "tool"

    def call(self, args: Sequence[Any] = ()) -> Any:
        return self.handler(*args)


@dataclass
class SemanticEnv:
    thoughts: dict[str, Thought | dict[str, Any]] = field(default_factory=dict)
    memories: dict[str, Memory] = field(default_factory=dict)
    tools: dict[str, ToolSpec] = field(default_factory=dict)
    trace: Trace = field(default_factory=Trace)

    def define_thought(self, name: str, steps: Iterable[Any],
                       params: Sequence[str] = ()) -> dict[str, Any]:
        ast = thought_ast(name, steps, params=params)
        self.thoughts[name] = ast
        self.trace.record(Effect("define", f"thought:{name}"), thought=name, node=ast)
        return ast

    def remember(self, key: str, value: Any, kind: str = "session",
                 meta: Optional[Mapping[str, Any]] = None) -> Memory:
        memory = self.memories.get(key)
        if memory:
            memory.update(value)
        else:
            memory = Memory(key=key, value=value, kind=kind, meta=dict(meta or {}))
            self.memories[key] = memory
        self.trace.record(Effect("memory.write", key, value))
        return memory

    def recall(self, key: str, default: Any = None) -> Any:
        memory = self.memories.get(key)
        result = memory.value if memory else default
        self.trace.record(Effect("memory.read", key), result=result, ok=memory is not None)
        return result

    def register_tool(self, tool: ToolSpec) -> None:
        self.tools[tool.name] = tool
        self.trace.record(Effect("tool.register", tool.name, {"risk": tool.risk}))

    def call_tool(self, name: str, args: Sequence[Any] = ()) -> Any:
        if name not in self.tools:
            self.trace.record(Effect("tool.call", name, list(args)), ok=False)
            raise KeyError(f"tool not found: {name}")
        result = self.tools[name].call(args)
        self.trace.record(Effect(self.tools[name].effect_kind, name, list(args)), result=result)
        return result


@dataclass
class DaoSemanticAdapter:
    dao_env: Any
    semantic_env: SemanticEnv = field(default_factory=SemanticEnv)

    def define_thought(self, name: str, steps: Iterable[Any],
                       params: Sequence[str] = (), doc: str = "") -> Thought:
        ast = self.semantic_env.define_thought(name, steps, params=params)
        thought = thought_from_ast(ast, register=True, doc=doc)
        self.dao_env.registry[name] = thought
        self.semantic_env.thoughts[name] = thought
        self.semantic_env.trace.record(
            Effect("dao.register", f"thought:{name}"),
            thought=name,
            node=ast,
        )
        return thought

    def register_tool(self, tool: ToolSpec) -> None:
        self.semantic_env.register_tool(tool)

        def _runtime_tool(*args: Any) -> Any:
            return self.semantic_env.call_tool(tool.name, list(args))

        self.dao_env.set(tool.name, _runtime_tool)
        self.semantic_env.trace.record(
            Effect("dao.expose_tool", tool.name, {"risk": tool.risk})
        )

    def remember(self, key: str, value: Any, kind: str = "session",
                 meta: Optional[Mapping[str, Any]] = None) -> Memory:
        return self.semantic_env.remember(key, value, kind=kind, meta=meta)

    def recall(self, key: str, default: Any = None) -> Any:
        return self.semantic_env.recall(key, default=default)

    def run_thought(self, name: str, args: Optional[Sequence[Any]] = None) -> Any:
        call_args = list(args or [])
        self.semantic_env.trace.record(
            Effect("thought.call", f"thought:{name}", call_args),
            thought=name,
        )
        try:
            result = self.dao_env.run(name, call_args)
        except Exception as exc:
            self.semantic_env.trace.record(
                Effect("thought.error", f"thought:{name}", {"error": str(exc)}),
                thought=name,
                ok=False,
            )
            raise
        self.semantic_env.trace.record(
            Effect("thought.result", f"thought:{name}"),
            thought=name,
            result=result,
        )
        return result


__all__ = [
    "Effect",
    "Memory",
    "Patch",
    "SemanticEnv",
    "ToolSpec",
    "Trace",
    "TraceEvent",
    "DaoSemanticAdapter",
    "block",
    "call",
    "expand_step",
    "lit",
    "node_to_dict",
    "ref",
    "thought_ast",
    "thought_from_ast",
    "value_to_node",
]
