"""Test arithmetic demo via MCP server."""
import json, os, subprocess, sys
from pathlib import Path

def test_arithmetic_via_mcp():
    env = dict(os.environ)
    env["PYTHONIOENCODING"] = "utf-8"
    proc = subprocess.Popen(
        [sys.executable, "-m", "dao.mcp_server_kernel", "demos"],
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
        # Initialize
        resp = send(1, "initialize")
        assert "result" in resp

        # List tools
        resp = send(2, "tools/list")
        tools = resp["result"]["tools"]
        tool_names = [t["name"] for t in tools]
        print(f"Available tools: {tool_names}")
        # Tools are now named by file stem
        assert "arithmetic" in tool_names

        # Call arithmetic: main(3, 4) should return 7
        resp = send(3, "tools/call", {"name": "arithmetic", "arguments": {"x": 3, "y": 4}})
        assert "result" in resp, f"call failed: {resp}"
        content = resp["result"]["content"]
        result = json.loads(content[0]["text"])
        print(f"arithmetic(3, 4) = {result}")
        assert result["result"] == 7

        print("Arithmetic MCP test passed")
    finally:
        proc.stdin.close()
        proc.wait(timeout=5)

test_arithmetic_via_mcp()
