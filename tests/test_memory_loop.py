"""可执行记忆闭环回归测试：存 → 换实例召回 → 执行 → Agent 用工具跑完 + MCP 记忆工具。"""
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

BINDINGS = Path(__file__).resolve().parent.parent / "bindings" / "python"
sys.path.insert(0, str(BINDINGS))

from dao_kernel import Agent, MemorySystem, MemoryType, Runtime, Thought
from dao_kernel.thought import _find_kernel_library


def test_memory_closed_loop():
    with tempfile.TemporaryDirectory() as tmp:
        double = Thought.from_source("翻倍", "思 main(x) { x * 2 }")
        answer = Thought.from_source("答案", "思 main() { 返 42 }")

        mem = MemorySystem(tmp)
        mem.store("翻倍", double, MemoryType.LONG_TERM)
        mem.store("答案", answer, MemoryType.FACT)
        assert len(mem) == 2

        # 全新实例（等价换进程）召回并执行
        mem2 = MemorySystem(tmp)
        assert mem2.recall("翻倍").call_i64(args=[21]) == 42
        assert mem2.recall("答案").call_i64(args=[]) == 42

        # 召回的念头注册成 Agent 工具，think-act-observe 跑完
        agent = Agent("得到答案 42", max_turns=5)
        agent.register_tool("答案", mem2.recall("答案"))
        with Runtime(_find_kernel_library()) as rt:
            result = agent.run(rt)
        assert result == 42


def test_memory_mcp_tools():
    env = dict(os.environ)
    env["PYTHONIOENCODING"] = "utf-8"
    env["DAO_MEMORY_DIR"] = tempfile.mkdtemp()

    proc = subprocess.Popen(
        [sys.executable, "-m", "dao.mcp_server_kernel"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, encoding="utf-8", errors="replace", env=env,
    )

    def send(req_id, method, params=None):
        msg = {"jsonrpc": "2.0", "id": req_id, "method": method}
        if params is not None:
            msg["params"] = params
        proc.stdin.write(json.dumps(msg, ensure_ascii=False) + "\n")
        proc.stdin.flush()
        line = proc.stdout.readline()
        assert line, proc.stderr.read()
        return json.loads(line)

    try:
        send(1, "initialize")
        resp = send(2, "tools/list")
        names = [t["name"] for t in resp["result"]["tools"]]
        assert "ku_memory_store" in names and "ku_memory_recall" in names

        send(3, "tools/call", {"name": "ku_memory_store",
                               "arguments": {"key": "答案", "source": "思 main() { 返 42 }"}})
        resp = send(4, "tools/call", {"name": "ku_memory_recall",
                                      "arguments": {"key": "答案", "args": []}})
        result = json.loads(resp["result"]["content"][0]["text"])
        assert result["result"] == 42
    finally:
        proc.stdin.close()
        proc.wait(timeout=10)
