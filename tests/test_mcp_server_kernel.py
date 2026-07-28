import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

def test_mcp_server_kernel():
    """Test the kernel-backed MCP server."""
    ku_dir = Path(tempfile.mkdtemp())
    
    # Create a simple .ku file with thought main as entry point
    ku_file = ku_dir / "arithmetic.ku"
    ku_file.write_text("""thought main(x, y) {
  return x + y
}
""", encoding="utf-8")
    
    # Start the MCP server
    env = dict(os.environ)
    env["PYTHONIOENCODING"] = "utf-8"
    proc = subprocess.Popen(
        [sys.executable, "-m", "dao.mcp_server_kernel", str(ku_dir)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=env,
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
        assert "result" in resp, f"initialize failed: {resp}"
        assert resp["result"]["serverInfo"]["name"] == "ku-kernel-mcp"
        
        # List tools
        resp = send(2, "tools/list")
        assert "result" in resp, f"tools/list failed: {resp}"
        tools = resp["result"]["tools"]
        tool_names = [t["name"] for t in tools]
        # Tool name is the file stem
        assert "arithmetic" in tool_names, f"arithmetic tool not found: {tool_names}"
        
        # Call tool
        resp = send(3, "tools/call", {"name": "arithmetic", "arguments": {"x": 10, "y": 20}})
        assert "result" in resp, f"tools/call failed: {resp}"
        content = resp["result"]["content"]
        assert len(content) == 1
        result = json.loads(content[0]["text"])
        assert result["result"] == 30, f"expected 30, got {result}"
        
        # Call unknown tool
        resp = send(4, "tools/call", {"name": "unknown", "arguments": {}})
        assert "error" in resp, f"expected error: {resp}"
        
        print("All MCP server kernel tests passed")
    finally:
        proc.stdin.close()
        proc.wait(timeout=5)

test_mcp_server_kernel()
