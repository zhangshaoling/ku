import json
import os
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA_DIR = ROOT / ".dao_data_l1_proto"  # 验收只在副本跑，严禁写主线 .dao_data


def send_request(proc, req_id, method, params=None):
    payload = {"jsonrpc": "2.0", "id": req_id, "method": method}
    if params is not None:
        payload["params"] = params
    proc.stdin.write(json.dumps(payload, ensure_ascii=True) + "\n")
    proc.stdin.flush()
    line = proc.stdout.readline()
    if not line:
        stderr = proc.stderr.read() if proc.stderr else ""
        raise RuntimeError(f"MCP server returned no response. stderr={stderr}")
    response = json.loads(line)
    if "error" in response:
        raise RuntimeError(response["error"])
    return response


def tool_payload(response):
    return json.loads(response["result"]["content"][0]["text"])


def main():
    env = os.environ.copy()
    env["DAO_DATA_DIR"] = str(DATA_DIR)
    env["PYTHONPATH"] = str(ROOT)

    proc = subprocess.Popen(
        [sys.executable, "-m", "dao.mcp_server"],
        cwd=ROOT,
        env=env,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    try:
        init = send_request(
            proc,
            1,
            "initialize",
            {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "dao-tiandao-verify", "version": "1"},
            },
        )
        server_name = init["result"]["serverInfo"]["name"]

        listed = send_request(proc, 2, "tools/list")
        tool_names = {tool["name"] for tool in listed["result"]["tools"]}
        required = {"ku_tiandao", "ku_tiandao_stats", "ku_eval"}
        missing = sorted(required - tool_names)
        if missing:
            raise AssertionError(f"missing MCP tools: {missing}")

        marker = f"CODEX_MCP_VERIFY_{int(time.time())}"
        question = "Codex " + "\u5df2\u8fde\u63a5\u5171\u4eab\u5929\u9053\u8bb0\u5fc6" + f" {marker}"
        tiandao = send_request(
            proc,
            3,
            "tools/call",
            {
                "name": "ku_tiandao",
                "arguments": {
                    "question": question,
                    "context": {"agent": "codex", "check": "verify_codex_mcp_tiandao"},
                },
            },
        )
        value = tool_payload(tiandao)["result"]
        if marker not in json.dumps(value, ensure_ascii=False):
            raise AssertionError(f"marker not found in Tiandao response: {value}")

        stats = send_request(proc, 4, "tools/call", {"name": "ku_tiandao_stats", "arguments": {}})
        stats_value = tool_payload(stats)["result"]

        print(f"server: {server_name}")
        print(f"tools: {len(tool_names)}")
        print(f"marker: {marker}")
        print(f"memory: {json.dumps(stats_value['memory'], ensure_ascii=False, sort_keys=True)}")
        print("codex mcp tiandao verification passed")
    finally:
        if proc.stdin:
            proc.stdin.close()
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    main()
