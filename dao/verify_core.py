"""Run the Dao core language self-check suite."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from dao.runtime import DaoEnv, Thought  # noqa: E402


def main() -> int:
    env = DaoEnv()
    env._dao_dir = str(Path(__file__).resolve().parent)
    env.load(str(Path(__file__).resolve().parent / "test_core.ku"), process_imports=True)

    runner = Thought.registry.get("运行核心自检")
    if not isinstance(runner, Thought):
        print("核心自检失败：找不到入口 运行核心自检")
        return 1

    result = runner.call([])
    return 0 if result is True else 1


if __name__ == "__main__":
    raise SystemExit(main())
