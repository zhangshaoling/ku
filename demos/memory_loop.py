"""可执行记忆闭环 demo — 证明 思 = 代码 = 记忆。

链：写 .ku 念头 → 编译成 .dao → 存进记忆(持久化) → 换一个新 MemorySystem
(等价换进程)召回 → 执行 → 把召回的记忆注册成 Agent 工具 → think-act-observe 跑完。

运行：
    .\tools\build_kernel.ps1
    .\.venv\Scripts\python.exe demos\memory_loop.py
"""
from __future__ import annotations

import sys
import tempfile
from pathlib import Path

BINDINGS_DIR = Path(__file__).resolve().parent.parent / "bindings" / "python"
sys.path.insert(0, str(BINDINGS_DIR))

from dao_kernel import (
    Agent, MemorySystem, MemoryType, Runtime, Thought, fnv1a,
)
from dao_kernel.thought import _find_kernel_library


def section(title: str) -> None:
    print(f"\n=== {title} ===")


def main() -> int:
    data_dir = Path(tempfile.mkdtemp(prefix="ku_memory_loop_"))
    print("数据目录:", data_dir)

    # ---- 阶段 1：编译 + 存入记忆 ----
    section("1) 编译并存入记忆（念头 = 代码 = 记忆）")
    double_src = "思 main(x) { x * 2 }"
    answer_src = "思 main() { 返 42 }"
    double = Thought.from_source("翻倍", double_src, doc="把输入翻倍")
    answer = Thought.from_source("答案", answer_src, doc="记住的答案")

    mem = MemorySystem(data_dir)
    mem.store("翻倍", double, MemoryType.LONG_TERM, meta={"note": "参数化念头"})
    mem.store("答案", answer, MemoryType.FACT, meta={"note": "零参事实念头"})
    print("已持久化:", ", ".join(mem.list_all()))
    print("落盘文件:", ", ".join(sorted(p.name for p in data_dir.glob("*"))))

    # ---- 阶段 2：换一个新 MemorySystem（等价换进程）召回并执行 ----
    section("2) 换新进程召回并执行（持久化证明）")
    mem2 = MemorySystem(data_dir)  # 全新实例，从磁盘重新加载
    assert mem2 is not mem and len(mem2) == 2, "内存应能跨实例恢复"

    double2 = mem2.recall("翻倍")
    r1 = double2.call_i64(args=[21])
    print(f"召回「翻倍」→ 执行(21) → {r1}")
    assert r1 == 42

    answer2 = mem2.recall("答案")
    r2 = answer2.call_i64(args=[])
    print(f"召回「答案」→ 执行() → {r2}")
    assert r2 == 42

    # ---- 阶段 3：把召回的记忆注册成 Agent 工具，跑 think-act-observe ----
    section("3) 记忆 → Agent 工具 → think-act-observe")
    agent = Agent("得到答案 42", max_turns=5)
    agent.register_tool("答案", answer2)
    with Runtime(_find_kernel_library()) as rt:
        result = agent.run(rt)
    print(f"目标「{agent.state.goal}」→ 结果 {result}，turns={agent.state.current_turn}")
    assert result is not None

    section("闭环成立：思 = 代码 = 记忆")
    print("一个念头，写下来是代码，存起来是记忆，召回就能跑，注册了就是工具。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
