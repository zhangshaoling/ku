from pathlib import Path

from dao.runtime import DaoEnv, Thought


ROOT = Path(__file__).resolve().parents[1]


def load_ku_std_module(name):
    Thought.registry.clear()
    env = DaoEnv()
    env.load(str(ROOT / "dao" / "std" / name))
    return env


def call_ku(name, args):
    return Thought.registry[name].call(args)


def normalize_empty(value):
    return None if value == "" else value


def normalize_value(value):
    value = normalize_empty(value)
    if isinstance(value, dict):
        result = {}
        for key, child in value.items():
            if key == "meta":
                continue
            result[key] = normalize_value(child)
        if result.get("type") and result.get("value") is None:
            result["value"] = ""
        if "children" not in result and result.get("type"):
            result["children"] = []
        if "value" not in result and result.get("type"):
            result["value"] = ""
        return result
    if isinstance(value, list):
        return [normalize_value(item) for item in value]
    return value


def normalize_effect(effect):
    return {
        "kind": effect["kind"],
        "target": effect["target"],
        "payload": normalize_value(effect.get("payload")),
        "meta": effect.get("meta", {}),
    }


def normalize_event(event):
    return {
        "thought": event.get("thought", ""),
        "effect": normalize_effect(event["effect"]),
        "node": normalize_value(event.get("node")),
        "result": normalize_value(event.get("result")),
        "ok": event["ok"],
    }
